#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "logic/components/transform.h"
#include "logic/terrain/chunk.h"
#include "logic/terrain/terrain_generator.h"
#include "logic/vegetation/forest.h"
#include "logic/vegetation/vegetation.h"

using namespace dy;

namespace {

VegetationLibrary testLibrary() {
    VegetationLibrary library;
    library.species = { { "oak", "Oak", "tree", 12.f, 6.f, 100, 3 }, { "pine", "Pine", "tree", 17.f, 5.f, 100, 3 },
        { "hazel", "Hazel", "shrub", 3.f, 1.5f, 100, 3 }, { "daisy", "Daisy", "ground", 0.3f, 0.2f, 10, 2 } };
    return library;
}

nlohmann::json testDefinition() {
    return nlohmann::json::parse(R"({
        "patch_size": 320, "coverage": 0.5, "max_slope": 24,
        "stands": [
            { "name": "oaks", "layers": [ { "where": "forest", "spacing": 7, "fill": 0.9, "species": { "oak": 3, "hazel": 1 } } ] },
            { "name": "pines", "layers": [ { "where": "forest", "spacing": 6, "fill": 0.9, "species": ["pine"] } ] }
        ],
        "everywhere": [
            { "where": "edge", "spacing": 5, "fill": 0.5, "species": ["hazel", "no_such_plant"] },
            { "where": "open", "spacing": 3, "fill": 0.6, "clump": 26, "max_slope": 35, "species": ["daisy"] }
        ]
    })");
}

// Density at a world position, trilinear
float densityAt(const TerrainGenerator& generator, glm::vec3 p) {
    glm::vec3 s = p / (float) chunk::cube_size;
    glm::ivec3 start = glm::ivec3(glm::floor(s));
    glm::vec3 f = s - glm::vec3(start);
    std::vector<float> v;
    generator.sample(start, glm::ivec3(2), v);
    auto at = [&](int x, int y, int z) { return v[(x * 2 + z) * 2 + y]; };
    float value = 0.f;
    for(int x = 0; x < 2; x++) for(int y = 0; y < 2; y++) for(int z = 0; z < 2; z++) {
        value += at(x, y, z) * (x ? f.x : 1.f - f.x) * (y ? f.y : 1.f - f.y) * (z ? f.z : 1.f - f.z);
    }
    return value;
}

struct ForestWorld {
    entt::registry reg;
    std::unique_ptr<ForestSys> sys;

    ForestWorld(std::shared_ptr<TerrainGenerator> generator, const nlohmann::json& params = nlohmann::json::object()) {
        reg.ctx().emplace<VegetationLibrary>(testLibrary());
        reg.ctx().emplace<std::shared_ptr<TerrainGenerator>>(generator);
        nlohmann::json p = params;
        p["definition"] = testDefinition();
        sys = std::make_unique<ForestSys>(reg, p);
        sys->preinit();
        sys->init();
    }

    ~ForestWorld() {
        sys->finish();
    }

    // Budget of 2 chunks a tick
    entt::entity add(glm::ivec3 pos) {
        auto& generator = *reg.ctx().get<std::shared_ptr<TerrainGenerator>>();
        std::vector<float> values;
        float mean;
        if(!generator.generate(pos, values, mean)) return entt::null;
        auto chunk = reg.create();
        reg.emplace<ChunkC>(chunk, pos);
        reg.emplace<ChunkVolume>(chunk).values = std::move(values);
        sys->tick(0.);
        return chunk;
    }

    std::vector<std::pair<glm::vec3, uint32_t>> plants(entt::entity chunk) {
        std::vector<std::pair<glm::vec3, uint32_t>> out;
        for(auto plant : reg.get<ChunkForest>(chunk).plants) out.emplace_back(reg.get<Transform>(plant).position, reg.get<Plant>(plant).species);
        return out;
    }
};

}

TEST_SUITE("unit") {

TEST_CASE("FOREST.1 forests cover part of the gentle land in patches and leave the steep land bare") {

    auto generator = std::make_shared<TerrainGenerator>();
    ForestParams params;
    params.patch_size = 320.f;
    ForestField field(generator, params);

    // 4 km by 4 km of nodes
    const int side = 500;
    std::vector<float> cover, stand;
    field.nodes(glm::ivec2(-side / 2), glm::ivec2(side), cover, stand);
    REQUIRE(cover.size() == (size_t) side * side);

    size_t thick = 0, open = 0, gentle = 0, gentle_forest = 0, steep = 0, low_stand = 0;
    for(int j = 0; j < side; j++) {
        for(int i = 0; i < side; i++) {
            float c = cover[(size_t) j * side + i];
            REQUIRE(c >= 0.f);
            REQUIRE(c <= 1.f);
            if(c > 0.95f) thick++;
            if(c < 0.05f) open++;
            if(stand[(size_t) j * side + i] < 0.5f) low_stand++;

            // Every 10th node : the slope takes single noise samples
            if(i % 10 || j % 10) continue;
            float x = (i - side / 2) * ForestField::node, z = (j - side / 2) * ForestField::node;
            float slope = field.landSlope(x, z);
            if(slope > params.max_slope + 1.f) {
                steep++;
                CHECK(c < 0.02f);
            } else if(slope < params.max_slope - params.slope_fade - 1.f) {
                gentle++;
                if(c > 0.5f) gentle_forest++;
            }
        }
    }

    // Thick forests and empty land, little in between
    const float total = (float) side * side;
    CHECK(thick / total > 0.15f);
    CHECK(open / total > 0.3f);
    CHECK((thick + open) / total > 0.8f);
    REQUIRE(steep > 20);
    REQUIRE(gentle > 200);
    CHECK((float) gentle_forest / gentle == doctest::Approx(params.coverage).epsilon(0.3));
    CHECK(low_stand / total == doctest::Approx(0.5f).epsilon(0.3));

    // A point reads the same field as a block of nodes
    CHECK(field.cover(-16.f, 24.f) == doctest::Approx(cover[(size_t) (side / 2 + 3) * side + side / 2 - 2]).epsilon(0.001));
    float between = field.cover(4.f, 0.f);
    float a = cover[(size_t) (side / 2) * side + side / 2], b = cover[(size_t) (side / 2) * side + side / 2 + 1];
    CHECK(between == doctest::Approx((a + b) * 0.5f).epsilon(0.001));

    // More coverage, more forest
    params.coverage = 0.8f;
    ForestField more(generator, params);
    std::vector<float> more_cover;
    more.nodes(glm::ivec2(-side / 2), glm::ivec2(side), more_cover, stand);
    size_t more_thick = 0;
    for(float c : more_cover) if(c > 0.95f) more_thick++;
    CHECK(more_thick > thick * 1.2f);

}

TEST_CASE("FOREST.2 the plants of a layer follow the cover and the stands and do not depend on who asks") {

    auto generator = std::make_shared<TerrainGenerator>();
    auto library = testLibrary();
    ForestDefinition definition = ForestDefinition::parse(testDefinition(), library);
    REQUIRE(definition.stands.size() == 2);
    REQUIRE(definition.layers.size() == 4);
    // The species the library does not have is dropped
    CHECK(definition.layers[2].species.size() == 1);
    CHECK(definition.species() == std::vector<uint32_t> { 0, 1, 2, 3 });
    CHECK_THROWS(ForestDefinition::parse(nlohmann::json::parse(R"({ "everywhere": [ { "where": "sea", "species": ["oak"] } ] })"), library));

    ForestField field(generator, definition.params);

    for(uint32_t l = 0; l < definition.layers.size(); l++) {
        const ForestLayer& layer = definition.layers[l];
        std::vector<ForestCandidate> all, parts;
        forestCandidates(field, layer, l, 2, glm::ivec2(-150), glm::ivec2(150), all);
        forestCandidates(field, layer, l, 2, glm::ivec2(-150), glm::ivec2(-20, 150), parts);
        forestCandidates(field, layer, l, 2, glm::ivec2(-20, -150), glm::ivec2(150, 31), parts);
        forestCandidates(field, layer, l, 2, glm::ivec2(-20, 31), glm::ivec2(150), parts);
        REQUIRE(all.size() > 100);
        REQUIRE(parts.size() == all.size());

        auto before = [](const ForestCandidate& a, const ForestCandidate& b) { return a.x != b.x ? a.x < b.x : a.z < b.z; };
        std::sort(all.begin(), all.end(), before);
        std::sort(parts.begin(), parts.end(), before);
        size_t thick = 0, open = 0;
        for(size_t i = 0; i < all.size(); i++) {
            REQUIRE(all[i].x == parts[i].x);
            REQUIRE(all[i].z == parts[i].z);
            REQUIRE(all[i].species == parts[i].species);
            REQUIRE(all[i].cover == doctest::Approx(field.cover(all[i].x, all[i].z)).epsilon(0.001));
            if(all[i].cover > 0.9f) thick++;
            if(all[i].cover < 0.1f) open++;

            // In its cell
            glm::vec2 cell = glm::vec2(all[i].x, all[i].z) / layer.spacing;
            CHECK(cell.x - std::floor(cell.x) > 0.09f);
            CHECK(cell.x - std::floor(cell.x) < 0.91f);
        }

        if(layer.where == ForestLayer::Where::Forest) {
            CHECK(open < all.size() * 0.01f);
            CHECK(thick > all.size() * 0.55f);
            for(auto& c : all) {
                // A stand keeps to its side of the stand noise, give or take the mixing
                float stand = field.stand(c.x, c.z);
                if(layer.stand == 0) CHECK(stand < 0.61f);
                else CHECK(stand > 0.39f);
            }
        } else if(layer.where == ForestLayer::Where::Open) {
            CHECK(thick < all.size() * 0.01f);
            CHECK(open > all.size() * 0.9f);
        } else {
            CHECK(thick + open < all.size() * 0.5f);
        }
    }

    // Thick forest : about one tree per cell
    std::vector<ForestCandidate> trees;
    forestCandidates(field, definition.layers[0], 0, 1, glm::ivec2(-300), glm::ivec2(300), trees);
    size_t thick_cells = 0;
    for(int x = -300; x < 300; x += 2) for(int z = -300; z < 300; z += 2) {
        if(field.cover((x + 0.5f) * 7.f, (z + 0.5f) * 7.f) > 0.99f) thick_cells += 4;
    }
    size_t thick_trees = 0;
    for(auto& c : trees) if(c.cover > 0.99f) thick_trees++;
    REQUIRE(thick_cells > 1000);
    CHECK((float) thick_trees / thick_cells == doctest::Approx(0.9f).epsilon(0.15));

}

TEST_CASE("FOREST.3 the forest system plants each chunk once on its ground and takes the plants away with it") {

    auto generator = std::make_shared<TerrainGenerator>();
    ForestWorld a(generator), b(generator);
    auto field = a.reg.ctx().get<std::shared_ptr<ForestField>>();
    CHECK(a.reg.ctx().get<VegetationPreload>().species.size() == 4);

    // The chunks around the origin that cross the surface, in another order in b
    std::vector<glm::ivec3> positions;
    for(int x = -2; x <= 1; x++) for(int z = -2; z <= 1; z++) {
        positions.push_back(glm::ivec3(x, (int) std::floor(generator->surfaceHeight(x * 256.f + 128.f, z * 256.f + 128.f) / chunk::length), z));
    }
    size_t total = 0, trees = 0;
    for(size_t i = 0; i < positions.size(); i++) {
        glm::ivec3 pos = positions[i];
        entt::entity chunk = a.add(pos);
        b.add(positions[positions.size() - 1 - i]);
        if(chunk == entt::null) continue;
        REQUIRE(a.reg.all_of<ChunkForest>(chunk));

        auto plants = a.plants(chunk);
        total += plants.size();
        int above_air = 0, below_solid = 0;
        for(auto& [position, species] : plants) {
            glm::vec3 local = position - glm::vec3(pos * chunk::length);
            CHECK(local.x >= 0.f);
            CHECK(local.x < (float) chunk::length);
            CHECK(local.z >= 0.f);
            CHECK(local.z < (float) chunk::length);
            if(densityAt(*generator, position + glm::vec3(0.f, 1.5f, 0.f)) <= 0.f) above_air++;
            if(densityAt(*generator, position - glm::vec3(0.f, 1.5f, 0.f)) > 0.f) below_solid++;

            float cover = field->cover(position.x, position.z);
            if(species < 2) {
                trees++;
                CHECK(cover > 0.f);
            } else if(species == 3) {
                CHECK(cover < 1.f);
            }
        }
        CHECK(above_air == (int) plants.size());
        CHECK(below_solid == (int) plants.size());
    }
    REQUIRE(total > 500);
    REQUIRE(trees > 100);

    // The same plants whatever the order the chunks came in
    for(glm::ivec3 pos : positions) {
        entt::entity in_a = entt::null, in_b = entt::null;
        for(auto [entity, chunk] : a.reg.view<ChunkC>().each()) if(chunk.pos == pos) in_a = entity;
        for(auto [entity, chunk] : b.reg.view<ChunkC>().each()) if(chunk.pos == pos) in_b = entity;
        if(in_a == entt::null) continue;
        REQUIRE(b.reg.valid(in_b));
        auto plants = a.plants(in_a), same = b.plants(in_b);
        REQUIRE(plants.size() == same.size());
        for(size_t i = 0; i < plants.size(); i++) {
            CHECK(plants[i].first == same[i].first);
            CHECK(plants[i].second == same[i].second);
        }
    }

    // Nothing is planted twice, and the plants go with their chunk
    size_t count = a.reg.view<Plant>().size();
    CHECK(count == total);
    a.sys->tick(0.);
    CHECK(a.reg.view<Plant>().size() == count);
    for(auto entity : std::vector<entt::entity>(a.reg.view<ChunkC>().begin(), a.reg.view<ChunkC>().end())) a.reg.destroy(entity);
    CHECK(a.reg.view<Plant>().empty());

    // Another seed, other forests
    ForestWorld c(generator, { { "seed", 5 } });
    CHECK(c.reg.ctx().get<std::shared_ptr<ForestField>>()->params.seed == 5);
    bool differs = false;
    for(float x = -1000.f; x <= 1000.f; x += 100.f) differs |= std::abs(c.reg.ctx().get<std::shared_ptr<ForestField>>()->cover(x, 0.f) - field->cover(x, 0.f)) > 0.5f;
    CHECK(differs);

}

// FOREST_MAP=<file.ppm> dynamical_tests -tc="FOREST.map*" --no-skip : 4 km around the origin at 4 m a pixel,
// green : forest cover, red : slope of the land (full at 45 degrees), blue : the stand
TEST_CASE("FOREST.map writes a map of the forests" * doctest::skip()) {

    const char* path = std::getenv("FOREST_MAP");
    REQUIRE(path);

    auto generator = std::make_shared<TerrainGenerator>();
    ForestParams params;
    if(const char* seed = std::getenv("FOREST_SEED")) params.seed = (uint32_t) std::atoi(seed);
    if(const char* size = std::getenv("FOREST_PATCH")) params.patch_size = (float) std::atof(size);
    if(const char* slope = std::getenv("FOREST_SLOPE")) params.max_slope = (float) std::atof(slope);
    if(const char* reach = std::getenv("FOREST_REACH")) params.slope_reach = (float) std::atof(reach);
    ForestField field(generator, params);

    const int side = 500, zoom = 2;
    std::vector<float> cover, stand;
    field.nodes(glm::ivec2(-side / 2), glm::ivec2(side), cover, stand);

    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << side * zoom << " " << side * zoom << "\n255\n";
    for(int j = 0; j < side * zoom; j++) {
        for(int i = 0; i < side * zoom; i++) {
            float x = (i / (float) zoom - side / 2) * ForestField::node, z = (j / (float) zoom - side / 2) * ForestField::node;
            size_t index = (size_t) (j / zoom) * side + i / zoom;
            unsigned char pixel[3] = {
                (unsigned char) std::clamp(field.landSlope(x, z) / 45.f * 255.f, 0.f, 255.f),
                (unsigned char) (cover[index] * 255.f),
                (unsigned char) (cover[index] > 0.f ? 60.f + stand[index] * 195.f : 0.f) };
            file.write((const char*) pixel, 3);
        }
    }
    CHECK(file.good());

}

}
