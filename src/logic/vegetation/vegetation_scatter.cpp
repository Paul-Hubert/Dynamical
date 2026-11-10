#include "vegetation_scatter.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "vegetation.h"

#include "logic/components/transform.h"
#include "logic/terrain/chunk.h"
#include "logic/terrain/terrain_generator.h"
#include "scene/system_registry.h"
#include "util/log.h"

using namespace dy;

namespace {

// Density of a chunk volume at fractional sample coordinates, linear in x and z, y on a sample
float density(const std::vector<float>& values, float x, float z, int y) {
    int x0 = (int) std::floor(x), z0 = (int) std::floor(z);
    float fx = x - x0, fz = z - z0;
    float a = values[chunk::index(x0, y, z0)], b = values[chunk::index(x0 + 1, y, z0)];
    float c = values[chunk::index(x0, y, z0 + 1)], d = values[chunk::index(x0 + 1, y, z0 + 1)];
    return (a * (1.f - fx) + b * fx) * (1.f - fz) + (c * (1.f - fx) + d * fx) * fz;
}

// Highest surface of the column inside the cubes the chunk owns : sample height (fractional), false in the air or underground
bool surface(const std::vector<float>& values, float x, float z, float& height) {
    for(int y = chunk::border + chunk::cubes - 1; y >= chunk::border; y--) {
        float below = density(values, x, z, y);
        float above = density(values, x, z, y + 1);
        if(below > 0.f && above <= 0.f) {
            height = y + below / (below - above);
            return true;
        }
    }
    return false;
}

// Height of the ground at (x, z), from the density function itself : works before any chunk is loaded
bool groundHeight(const TerrainGenerator& generator, float x, float z, float& height) {

    // Two columns of samples in x and z around the point, 60 units above and below the heightfield
    const float guess = generator.surfaceHeight(x, z);
    const int count = 30;
    glm::ivec3 start((int) std::floor(x / chunk::cube_size), (int) std::floor(guess / chunk::cube_size) - count / 2, (int) std::floor(z / chunk::cube_size));
    std::vector<float> values;
    generator.sample(start, glm::ivec3(2, count, 2), values);

    float fx = x / chunk::cube_size - start.x, fz = z / chunk::cube_size - start.z;
    auto at = [&](int y) {
        // [x][z][y]
        float a = values[(0 * 2 + 0) * count + y], b = values[(1 * 2 + 0) * count + y];
        float c = values[(0 * 2 + 1) * count + y], d = values[(1 * 2 + 1) * count + y];
        return (a * (1.f - fx) + b * fx) * (1.f - fz) + (c * (1.f - fx) + d * fx) * fz;
    };
    for(int y = count - 2; y >= 0; y--) {
        float below = at(y), above = at(y + 1);
        if(below > 0.f && above <= 0.f) {
            height = (start.y + y + below / (below - above)) * chunk::cube_size;
            return true;
        }
    }
    return false;

}

uint32_t hash(glm::ivec3 pos, uint32_t seed) {
    uint32_t h = seed * 0x9E3779B9u;
    for(int i = 0; i < 3; i++) {
        h ^= (uint32_t) pos[i] + 0x9E3779B9u + (h << 6) + (h >> 2);
        h *= 0x85EBCA6Bu;
        h ^= h >> 13;
    }
    return h;
}

// "species" (ids) or "categories", cut down to "max_species" with "seed" : indices in the library, sorted
std::vector<uint32_t> selectSpecies(const VegetationLibrary& library, const nlohmann::json& params, const char* who) {

    std::vector<uint32_t> species;

    if(auto it = params.find("species"); it != params.end() && it->is_array() && !it->empty()) {
        for(auto& id : *it) {
            int index = library.find(id.get<std::string>());
            if(index < 0) dy::log(dy::warning) << who << " : no species " << id.get<std::string>() << " in the vegetation library\n";
            else species.push_back((uint32_t) index);
        }
        return species;
    }

    std::vector<std::string> categories { "tree" };
    if(auto it = params.find("categories"); it != params.end()) categories = it->get<std::vector<std::string>>();
    for(uint32_t i = 0; i < library.species.size(); i++) {
        if(std::find(categories.begin(), categories.end(), library.species[i].category) != categories.end()) species.push_back(i);
    }

    int max_species = params.value("max_species", 0);
    if(max_species > 0 && (int) species.size() > max_species) {
        // Not std::shuffle : its result depends on the standard library
        std::mt19937 rng(params.value("seed", 1u));
        for(size_t i = 0; i < (size_t) max_species; i++) {
            size_t j = i + rng() % (species.size() - i);
            std::swap(species[i], species[j]);
        }
        species.resize(max_species);
        std::sort(species.begin(), species.end());
    }

    return species;

}

nlohmann::json selectionOf(const nlohmann::json& params) {
    nlohmann::json selection = nlohmann::json::object();
    for(const char* key : { "species", "categories", "max_species", "seed" }) {
        if(auto it = params.find(key); it != params.end()) selection[key] = *it;
    }
    return selection;
}

}

VegetationScatterSys::VegetationScatterSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    per_chunk = params.value("per_chunk", per_chunk);
    seed = params.value("seed", seed);
    max_slope = params.value("max_slope", max_slope);
    if(auto it = params.find("scale"); it != params.end() && it->is_array() && it->size() == 2) {
        scale_min = it->at(0).get<float>();
        scale_max = it->at(1).get<float>();
    }
    selection = selectionOf(params);
}

void VegetationScatterSys::preinit() {
    reg.storage<Plant>();
    reg.storage<ChunkPlants>();
    reg.on_destroy<ChunkPlants>().connect<&VegetationScatterSys::onChunkPlantsDestroyed>(*this);
}

void VegetationScatterSys::init() {

    auto& library = vegetationLibrary(reg);
    species = selectSpecies(library, selection, "VegetationScatter");

    if(!species.empty()) {
        auto& out = dy::log(dy::info) << "VegetationScatter : " << per_chunk << " per chunk among";
        for(uint32_t s : species) out << " " << library.species[s].id;
        out << "\n";
    }

}

void VegetationScatterSys::scatter(entt::entity entity) {

    const ChunkC& chunk_c = reg.get<ChunkC>(entity);
    const std::vector<float>& values = reg.get<ChunkVolume>(entity).values;
    auto& plants = reg.emplace<ChunkPlants>(entity).plants;
    if(values.size() != chunk::num_values) return;

    const auto& library = reg.ctx().get<VegetationLibrary>();
    const float min_up = std::cos(glm::radians(max_slope));

    std::mt19937 rng(hash(chunk_c.pos, seed));
    auto unit = [&]() { return (float) (rng() >> 8) / 16777216.f; };

    for(int i = 0; i < per_chunk; i++) {

        // Every draw happens whether the plant is kept or not, so a plant does not depend on the ones before it
        float x = chunk::border + unit() * chunk::cubes;
        float z = chunk::border + unit() * chunk::cubes;
        uint32_t pick = species[std::min((size_t) (unit() * species.size()), species.size() - 1)];
        float yaw = unit() * 6.2831853f;
        float scale = scale_min + unit() * (scale_max - scale_min);

        float y;
        if(!surface(values, x, z, y)) continue;

        // Slope from the heights one sample away
        float xa, xb, za, zb;
        float x0 = std::max(x - 1.f, 0.f), x1 = std::min(x + 1.f, (float) chunk::values - 1.001f);
        float z0 = std::max(z - 1.f, 0.f), z1 = std::min(z + 1.f, (float) chunk::values - 1.001f);
        if(!surface(values, x0, z, xa) || !surface(values, x1, z, xb) || !surface(values, x, z0, za) || !surface(values, x, z1, zb)) continue;
        glm::vec3 normal = glm::normalize(glm::vec3((xa - xb) / (x1 - x0), 1.f, (za - zb) / (z1 - z0)));
        if(normal.y < min_up) continue;

        glm::vec3 position = glm::vec3(chunk_c.pos * chunk::length) + (glm::vec3(x, y, z) - (float) chunk::border) * (float) chunk::cube_size;
        // Sunk a little, so the foot of the trunk is not in the air on the downhill side
        position.y -= std::min(0.02f * library.species[pick].height, 0.15f) * scale * (1.f + 4.f * (1.f - normal.y));

        auto plant = reg.create();
        reg.emplace<Transform>(plant, position, glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)), glm::vec3(scale));
        reg.emplace<Plant>(plant, pick);
        plants.push_back(plant);
    }

}

void VegetationScatterSys::tick(double dt) {

    OPTICK_EVENT();

    if(species.empty() || per_chunk <= 0) return;

    std::vector<entt::entity> pending;
    for(auto entity : reg.view<ChunkC, ChunkVolume>(entt::exclude<ChunkPlants, ChunkDestroying>)) {
        pending.push_back(entity);
    }
    for(auto entity : pending) {
        scatter(entity);
    }

}

void VegetationScatterSys::onChunkPlantsDestroyed(entt::registry& registry, entt::entity chunk) {
    for(auto plant : registry.get<ChunkPlants>(chunk).plants) {
        if(registry.valid(plant)) registry.destroy(plant);
    }
}

void VegetationScatterSys::finish() {
    reg.on_destroy<ChunkPlants>().disconnect<&VegetationScatterSys::onChunkPlantsDestroyed>(*this);
}



VegetationLineupSys::VegetationLineupSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    spacing = params.value("spacing", spacing);
    lods = params.value("lods", lods);
    if(auto it = params.find("origin"); it != params.end() && it->is_array() && it->size() == 2) {
        origin = glm::vec2(it->at(0).get<float>(), it->at(1).get<float>());
    }
    selection = selectionOf(params);
}

void VegetationLineupSys::init() {

    auto& library = vegetationLibrary(reg);
    auto species = selectSpecies(library, selection, "VegetationLineup");
    auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>();
    if(species.empty() || !generator) return;

    float width = 0.f;
    for(uint32_t s : species) width += 2.f * library.species[s].radius + spacing;

    float x = origin.x - width * 0.5f;
    for(uint32_t s : species) {
        const auto& entry = library.species[s];
        x += entry.radius + spacing * 0.5f;

        // The first one picks its level with the distance, the copies behind it are held at levels 0, 1, ...
        for(int row = 0; row <= (lods ? (int) entry.lods : 0); row++) {
            glm::vec3 position(x, 0.f, origin.y - row * (2.f * entry.radius + spacing));
            if(!groundHeight(**generator, position.x, position.z, position.y)) position.y = (*generator)->surfaceHeight(position.x, position.z);

            auto plant = reg.create();
            reg.emplace<Transform>(plant, position);
            reg.emplace<Plant>(plant, s, row - 1);
            auto& out = dy::log(dy::info) << "VegetationLineup : " << entry.id;
            if(row > 0) out << " level " << row - 1;
            out << " at " << position.x << " " << position.y << " " << position.z;
            if(row == 0) out << " (" << entry.height << " m)";
            out << "\n";
        }

        x += entry.radius + spacing * 0.5f;
    }

}

void dy::registerVegetation() {
    registerSystem("VegetationScatter", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<VegetationScatterSys>(reg, params); },
        SystemScope::Host });
    registerSystem("VegetationLineup", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<VegetationLineupSys>(reg, params); },
        SystemScope::Host });
}
