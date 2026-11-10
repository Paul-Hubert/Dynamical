#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "logic/components/transform.h"
#include "logic/terrain/chunk.h"
#include "logic/terrain/terrain_generator.h"
#include "logic/vegetation/vegetation.h"
#include "logic/vegetation/vegetation_scatter.h"

using namespace dy;

namespace {

// A library of one species : a quad, two levels of detail
std::filesystem::path writeLibrary(const std::string& name, uint32_t bad_index = 0) {

    auto dir = std::filesystem::temp_directory_path() / "dynamical_tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "species");

    nlohmann::json library = { { "format_version", 1 }, { "species", { {
        { "id", "quad" }, { "name", "Quad" }, { "category", "tree" }, { "height", 2.f }, { "radius", 1.f }, { "triangles", 2 }, { "lods", 2 } } } } };
    std::ofstream(dir / "library.json") << library.dump();

    nlohmann::json species = {
        { "id", "quad" }, { "bounds", { { "min", { -1, 0, 0 } }, { "max", { 1, 2, 0 } } } }, { "alpha_cutoff", 0.4f },
        { "vertex_count", 7 }, { "index_count", 9 },
        { "materials", { { { "name", "leaf" }, { "two_sided", true }, { "billboard", false }, { "color", "textures/leaf.png" }, { "normal", nullptr },
                           { "extra", nullptr }, { "subsurface", nullptr }, { "tint", { 1, 0.5, 0.25, 1 } } } } },
        { "lods", {
            { { "first_vertex", 0 }, { "vertex_count", 4 }, { "screen_height", 0.5 }, { "billboard", false },
              { "draws", { { { "material", 0 }, { "first_index", 0 }, { "index_count", 6 } } } } },
            { { "first_vertex", 4 }, { "vertex_count", 3 }, { "screen_height", 0.1 }, { "billboard", true },
              { "draws", { { { "material", 0 }, { "first_index", 6 }, { "index_count", 3 } } } } } } } };
    std::ofstream(dir / "species" / "quad.json") << species.dump();

    std::vector<VegetationVertex> vertices(7);
    for(size_t i = 0; i < vertices.size(); i++) vertices[i].position = glm::vec3((float) i, 0.f, 0.f);
    std::vector<uint32_t> indices { 0, 1, 2, 0, 2, 3, 0, 1, 2 };
    indices[8] += bad_index;
    std::ofstream bin(dir / "species" / "quad.bin", std::ios::binary);
    bin.write(reinterpret_cast<const char*>(vertices.data()), (std::streamsize) (vertices.size() * sizeof(VegetationVertex)));
    bin.write(reinterpret_cast<const char*>(indices.data()), (std::streamsize) (indices.size() * sizeof(uint32_t)));

    return dir;

}

struct ScatterWorld {
    entt::registry reg;
    entt::entity chunk = entt::null;
    std::unique_ptr<VegetationScatterSys> sys;

    ScatterWorld(const TerrainGenerator& generator, glm::ivec3 pos, const std::vector<float>& values, const nlohmann::json& params) {
        auto& library = reg.ctx().emplace<VegetationLibrary>();
        library.species = { { "oak", "Oak", "tree", 12.f, 6.f, 100, 3 }, { "pine", "Pine", "tree", 17.f, 5.f, 100, 3 }, { "fern", "Fern", "ground", 0.3f, 0.2f, 10, 2 } };
        chunk = reg.create();
        reg.emplace<ChunkC>(chunk, pos);
        reg.emplace<ChunkVolume>(chunk).values = values;
        sys = std::make_unique<VegetationScatterSys>(reg, params);
        sys->preinit();
        sys->init();
        sys->tick(0.);
    }

    ~ScatterWorld() {
        sys->finish();
    }

    std::vector<std::pair<glm::vec3, uint32_t>> plants() {
        std::vector<std::pair<glm::vec3, uint32_t>> out;
        for(auto [entity, transform, plant] : reg.view<Transform, Plant>().each()) out.emplace_back(transform.position, plant.species);
        return out;
    }
};

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

}

TEST_SUITE("unit") {

TEST_CASE("VEG.1 the vegetation library reads its index and a species and its geometry") {

    VegetationLibrary library;
    CHECK(!library.load(std::filesystem::temp_directory_path() / "dynamical_tests" / "no_such_library"));
    CHECK(library.species.empty());

    REQUIRE(library.load(writeLibrary("vegetation_ok")));
    REQUIRE(library.species.size() == 1);
    CHECK(library.find("quad") == 0);
    CHECK(library.find("oak") == -1);
    CHECK(library.species[0].category == "tree");
    CHECK(library.species[0].lods == 2);

    VegetationSpeciesDef def = library.loadSpecies(0);
    CHECK(def.alpha_cutoff == doctest::Approx(0.4f));
    CHECK(def.max.y == doctest::Approx(2.f));
    REQUIRE(def.materials.size() == 1);
    CHECK(def.materials[0].two_sided);
    CHECK(def.materials[0].color == "textures/leaf.png");
    CHECK(def.materials[0].normal.empty());
    CHECK(def.materials[0].tint.g == doctest::Approx(0.5f));
    REQUIRE(def.lods.size() == 2);
    CHECK(def.lods[1].first_vertex == 4);
    CHECK(def.lods[1].billboard);
    CHECK(def.lods[1].draws[0].first_index == 6);

    std::vector<uint32_t> indices;
    auto vertices = library.loadGeometry(def, indices);
    CHECK(vertices.size() == 7);
    CHECK(indices.size() == 9);
    CHECK(vertices[5].position.x == doctest::Approx(5.f));

    // An index past the vertices of its level of detail
    VegetationLibrary bad;
    REQUIRE(bad.load(writeLibrary("vegetation_bad_index", 1)));
    auto bad_def = bad.loadSpecies(0);
    CHECK_THROWS(bad.loadGeometry(bad_def, indices));

}

TEST_CASE("VEG.2 scattered plants stand on the surface and repeat for a seed and go with their chunk") {

    TerrainGenerator generator;

    // A chunk crossing the surface under (100, 100)
    glm::ivec3 pos(0, (int) std::floor(generator.surfaceHeight(100.f, 100.f) / chunk::length), 0);
    std::vector<float> values;
    float mean;
    REQUIRE(generator.generate(pos, values, mean));

    nlohmann::json params = { { "per_chunk", 60 }, { "seed", 7 }, { "categories", { "tree" } } };
    ScatterWorld a(generator, pos, values, params);
    ScatterWorld b(generator, pos, values, params);

    auto plants = a.plants();
    REQUIRE(plants.size() > 10);
    CHECK(plants.size() <= 60);
    CHECK(a.reg.get<ChunkPlants>(a.chunk).plants.size() == plants.size());

    auto same = b.plants();
    REQUIRE(same.size() == plants.size());
    int above_air = 0, below_solid = 0;
    for(size_t i = 0; i < plants.size(); i++) {
        CHECK(plants[i].first == same[i].first);
        CHECK(plants[i].second == same[i].second);

        // Only the trees of the library, inside the chunk
        CHECK(plants[i].second < 2);
        glm::vec3 local = plants[i].first - glm::vec3(pos * chunk::length);
        CHECK(local.x >= 0.f);
        CHECK(local.x <= (float) chunk::length);
        CHECK(local.z >= 0.f);
        CHECK(local.z <= (float) chunk::length);

        if(densityAt(generator, plants[i].first + glm::vec3(0.f, 1.5f, 0.f)) <= 0.f) above_air++;
        if(densityAt(generator, plants[i].first - glm::vec3(0.f, 1.5f, 0.f)) > 0.f) below_solid++;
    }
    CHECK(above_air == (int) plants.size());
    CHECK(below_solid == (int) plants.size());

    // Another seed moves them
    nlohmann::json other = params;
    other["seed"] = 8;
    ScatterWorld c(generator, pos, values, other);
    auto moved = c.plants();
    REQUIRE(!moved.empty());
    CHECK(moved[0].first != plants[0].first);

    // Nothing is scattered twice, and the plants are destroyed with the chunk
    a.sys->tick(0.);
    CHECK(a.plants().size() == plants.size());
    a.reg.destroy(a.chunk);
    CHECK(a.plants().empty());

}

}
