#ifndef DY_GRASS_MODEL_H
#define DY_GRASS_MODEL_H

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace dy {

// What a tile is made of : grass, and the plants that grow with it (docs/grass/13-plants.md)
enum class GrassKind : uint32_t {
    Meadow,         // tufts of straight blades : the grass of the engine
    Fescue,         // fine fescue : dense tufts of hair thin, arching blades
    Cocksfoot,      // clumps of broad leaves with hanging tips
    Ryegrass,       // thin stems with a flat spike of alternating spikelets (wheat, much thinner)
    Timothy,        // stems with a dense cylindrical head
    Oat,            // stems with an open panicle of hanging spikelets
    Clover,         // three leaflets on a stalk
    CloverFlower,   // clover with its round white heads
    Plantain,       // ribwort : a rosette of lance shaped leaves, heads on bare stalks
    Dandelion,      // a flat rosette of toothed leaves, yellow flowers and buds
};
constexpr int grass_kinds = 10;

// What a triangle belongs to (GrassModel::parts) : it gives the vertices their colour
enum class GrassPart : uint8_t { Blade, Stem, Leaf, Flower, Seed };

struct GrassKindInfo {
    const char* name;       // on the command line
    const char* label;
    float tile_size;        // metres the tile was modelled for
    glm::vec3 flower;       // colour of its GrassPart::Flower and Seed, linear
    glm::vec3 seed;
    bool small;             // grows where the grass is short only (logic/grass/grass_species.h)
    float abundance;        // added to its noise where the kinds are chosen : more of it
};
const GrassKindInfo& grassKindInfo(GrassKind kind);
bool grassKindFromName(const std::string& name, GrassKind& kind);

// Of the blades at the ground and at the top of the grass, linear (grass.frag.glsl has the same) : the
// ground between the blades is the first
inline const glm::vec3 grass_root_colour(0.04f, 0.13f, 0.03f);
inline const glm::vec3 grass_tip_colour(0.26f, 0.5f, 0.12f);

// A tile of grass blades : 1 x 1 on the ground (roots with x and z in [0, 1)), y up, the tallest tip at
// max_height. Leaning blades may leave the tile, which is meant to be repeated in a grid (GrassTracer).
struct GrassModel {
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;     // smooth, one per vertex
    std::vector<float> ids;             // one per vertex : a random value in 0..1, the same over a blade
    std::vector<uint8_t> parts;         // one per vertex : GrassPart
    std::vector<glm::vec3> colours;     // one per vertex, linear : what the rays bake in their colour map
    std::vector<uint32_t> indices;      // triangles
    uint32_t blades = 0;                // blades, leaves, stems, heads
    float max_height = 0.f;
    // Metres the tile and its tallest tip were modelled at : x and z times tile_size, y times
    // height / max_height give the plants their proportions
    float tile_size = 1.f, height = 1.f;
};

struct GrassModelParams {
    GrassKind kind = GrassKind::Meadow;
    uint32_t seed = 7;
    float max_height = 0.98f;       // under 1 : the baked rays start at y = 1
    // The rest is for GrassKind::Meadow, the other kinds have their own recipe
    int tufts = 9;                  // tufts x tufts on a jittered grid
    int blades_min = 4;             // blades of a tuft
    int blades_max = 7;
    float height_min = 0.45f;       // of a tuft, before the tallest tip is scaled to max_height
    float height_max = 1.f;
    float width = 0.024f;           // of a blade at the root
    float lean = 0.3f;              // how far a tip goes sideways, in blade heights
    float fold = 0.35f;             // depth of the V of a blade, in half widths
    int segments = 5;               // along a blade
};

// Deterministic for a seed
GrassModel generateGrassTile(const GrassModelParams& params = {});

// Wavefront OBJ of the tile, to look at the model in another tool : a group per part.
// metres : in the proportions it was modelled at, else the 1 x 1 tile the rays are baked on
bool writeGrassObj(const GrassModel& model, const std::string& path, bool metres = false);

}

#endif
