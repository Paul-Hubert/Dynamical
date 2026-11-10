#ifndef DY_FOREST_H
#define DY_FOREST_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "FastNoise/FastNoise.h"

#include "logic/systems/system.h"
#include "logic/vegetation/vegetation.h"

namespace dy {

class TerrainGenerator;

struct ForestParams {
    uint32_t seed = 1;
    float patch_size = 320.f;       // metres : size of the forests and of the open land between them
    float coverage = 0.5f;          // part of the gentle land under forest
    float edge = 0.1f;              // part of the land where the forest thins out to nothing
    float max_slope = 24.f;         // degrees : slope of the land no forest grows on
    float slope_fade = 9.f;         // degrees under max_slope over which the forest thins
    float slope_reach = 48.f;       // metres : the slope of the land is taken between two points this far on each side
    float stand_size = 130.f;       // metres : size of the stands (kinds of forest) inside the forests
};

// Where the forests are : a cover from 0 (open grass) to 1 (thick forest) and the kind of forest (stand), from
// patch noise and the slope of the land. Defined on the nodes of a lattice of `node` metres anchored to the world,
// linear between them, so a chunk and a point query see the same field. Safe to call from several threads.
class ForestField {
public:
    static constexpr float node = 8.f;

    ForestField(std::shared_ptr<const TerrainGenerator> generator, ForestParams params);

    // The nodes (start + (i, j)) * node, laid out [j][i]. stand is uniform in [0, 1] over the world
    void nodes(glm::ivec2 start, glm::ivec2 count, std::vector<float>& cover, std::vector<float>& stand) const;

    float cover(float x, float z) const;
    float stand(float x, float z) const;

    // Slope of the heightfield part of the terrain over twice slope_reach, degrees
    float landSlope(float x, float z) const;

    const ForestParams params;

private:
    std::shared_ptr<const TerrainGenerator> generator;
    FastNoise::SmartNode<> patches;
    FastNoise::SmartNode<> stands;
    float cover_low = 0.f, cover_high = 0.f;    // patch noise where the forest starts and where it is thick
    std::vector<float> stand_sorted;            // samples of the stand noise, to make it uniform
};

// A layer of plants : one candidate per cell of a grid of `spacing` metres anchored to the world, kept with a
// probability that follows the forest cover
struct ForestLayer {
    enum class Where { Forest, Edge, Open, All } where = Where::Forest;
    float spacing = 6.f;
    float fill = 1.f;               // part of the cells that hold a plant where the layer is at its densest
    float clump = 0.f;              // metres : the layer comes in clumps of this size, 0 for an even layer
    float max_slope = 30.f;         // degrees, of the ground under the plant
    float scale_min = 0.8f, scale_max = 1.2f;
    int stand = -1;                 // only in this stand, -1 : in all of them
    std::vector<uint32_t> species;  // indices in the library
    std::vector<float> weights;     // cumulative, the last one is the total
};

struct ForestDefinition {
    ForestParams params;
    std::vector<std::string> stands;
    std::vector<ForestLayer> layers;

    // { "patch_size": .., "coverage": .., ..., "stands": [ { "name": "pines", "layers": [ {
    //       "where": "forest" | "edge" | "open" | "all", "spacing": 6, "fill": 1, "clump": 0, "max_slope": 30,
    //       "scale": [0.8, 1.2], "species": { "<id>": weight, ... } } ] } ],
    //   "everywhere": [ layers of every stand and of the open land ] }
    // Species the library does not have are left out with a warning. Throws std::runtime_error on a malformed file
    static ForestDefinition parse(const nlohmann::json& j, const VegetationLibrary& library);

    // Every species of the layers, sorted
    std::vector<uint32_t> species() const;
};

// One plant of a layer, before it is put on the ground
struct ForestCandidate {
    float x = 0.f, z = 0.f;
    uint32_t species = 0;
    float yaw = 0.f;
    float scale = 1.f;
    float cover = 0.f;
};

// The plants of `layer` (its index is `index`) whose cell is in [min, max) cells. Only depends on the field,
// the layer and the cell : the same plants whatever the chunk that asks
void forestCandidates(const ForestField& field, const ForestLayer& layer, uint32_t index, int stands,
    glm::ivec2 min, glm::ivec2 max, std::vector<ForestCandidate>& out);

// On a chunk : the plants ForestSys put on it, destroyed with the chunk
struct ChunkForest {
    std::vector<entt::entity> plants;
};

// Forests in patches on the gentle parts of the terrain, open grass elsewhere (docs/forest.md).
// Makes the context's std::shared_ptr<ForestField>; needs the ChunkGenerator.
//
// params : "definition" (resources path, "forest/tautavel.forest.json"), and to override the definition's :
//          "seed", "patch_size", "coverage", "max_slope"
class ForestSys : public System {
public:
    ForestSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "Forest"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override;
    void finish() override;

private:
    void scatter(entt::entity chunk);
    void onChunkForestDestroyed(entt::registry& reg, entt::entity chunk);

    nlohmann::json params;
    ForestDefinition definition;
    std::shared_ptr<ForestField> field;
};

void registerForest();

}

#endif
