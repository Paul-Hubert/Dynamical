#ifndef DY_VEGETATION_SCATTER_H
#define DY_VEGETATION_SCATTER_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "logic/systems/system.h"

namespace dy {

// On a chunk : the plants scattered on it, destroyed with the chunk
struct ChunkPlants {
    std::vector<entt::entity> plants;
};

// Test scattering : a few random plants of the vegetation library on every chunk crossing the surface,
// to look at the species in the world. Deterministic for a seed, whatever the order chunks load in.
//
// params : "per_chunk" (6), "seed" (1), "scale" ([0.8, 1.2]), "max_slope" (degrees, 35),
//          "categories" (["tree"]) : every species of these categories, or
//          "species" ([ids]) : exactly these,
//          "max_species" (0 : no limit) : keeps this many of them, picked with the seed (the same params pick the
//          same species in VegetationLineup)
class VegetationScatterSys : public System {
public:
    VegetationScatterSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "VegetationScatter"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override;
    void finish() override;

private:
    void scatter(entt::entity chunk);
    void onChunkPlantsDestroyed(entt::registry& reg, entt::entity chunk);

    int per_chunk = 6;
    uint32_t seed = 1;
    float scale_min = 0.8f;
    float scale_max = 1.2f;
    float max_slope = 35.f;
    nlohmann::json selection;

    std::vector<uint32_t> species;      // indices in the library
};

// One plant of each selected species in a row on the terrain, to compare them side by side.
// The positions are logged. Needs the ChunkGenerator (its TerrainGenerator).
//
// params : "categories" / "species" / "max_species" / "seed" as VegetationScatter,
//          "origin" ([x, z] of the middle of the row, [0, -40]), "spacing" (gap between two crowns, 4),
//          "lods" (false) : behind each plant, one copy held at each of its levels of detail (the billboard last)
class VegetationLineupSys : public System {
public:
    VegetationLineupSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "VegetationLineup"; }
    void init() override;
    void tick(double dt) override {}

private:
    nlohmann::json selection;
    glm::vec2 origin = glm::vec2(0.f, -40.f);
    float spacing = 4.f;
    bool lods = false;
};

void registerVegetation();

}

#endif
