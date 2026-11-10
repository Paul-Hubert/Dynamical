#ifndef DY_GRASS_BAKE_H
#define DY_GRASS_BAKE_H

#include "grass_model.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace dy {

// Rays against a grass tile repeated to infinity on the ground plane (y = 0), which stops every ray
// that comes down : a baked ray always hits something.
class GrassTracer {
public:
    struct Hit {
        float t = 0.f;
        glm::vec3 normal = glm::vec3(0.f, 1.f, 0.f);   // faces the ray
        bool blade = false;                             // else the ground
        float id = 0.5f;                                // of the blade (GrassModel::ids), 0.5 on the ground
    };

    explicit GrassTracer(GrassModel tile, int cells = 48);

    // direction does not need to be normalized, t is in units of its length
    bool trace(glm::vec3 origin, glm::vec3 direction, Hit& hit, float max_t = 1e4f) const;

    const GrassModel& model() const { return tile; }

private:
    struct Entry {
        uint32_t triangle;      // first index
        int16_t shift_x, shift_z;   // copy of the tile that reaches this cell
    };

    GrassModel tile;
    int cells;
    std::vector<std::vector<Entry>> grid;   // cells x cells over one tile, [z * cells + x]
};

struct GrassBakeParams {
    int size = 256;         // samples of a tile along x and z
    int angles = 64;        // azimuths of the 45 degree rays
    GrassModelParams model;
};

// What the grass shader looks up instead of tracing rays (grass.frag.glsl).
// A texel is RGBA8 : r height of the hit (0 ground, 1 top of the grass), g b its normal
// (encodeGrassNormal), a the id of the blade (0.5 on the ground).
// The tile is (x, z) in [0, 1)², texel centers at (i + 0.5) / size.
struct GrassRaycast {
    int size = 0;
    int angles = 0;
    uint64_t key = 0;       // grassBakeKey of the parameters it was baked with

    // [angle][z][x] : ray going down at 45 degrees towards the azimuth (angle + 0.5) / angles * 2 pi
    // (x to z) that crosses the middle of the grass (y = 0.5) above the texel
    std::vector<uint8_t> oblique;
    // [z][x] : ray straight down on the texel
    std::vector<uint8_t> top;
};

// Changes with the parameters and with the baker itself : a cached file with another key is stale
uint64_t grassBakeKey(const GrassBakeParams& params);

// threads = 0 : one per core
GrassRaycast bakeGrass(const GrassBakeParams& params = {}, unsigned threads = 0);

bool saveGrassRaycast(const GrassRaycast& data, const std::string& path);
bool loadGrassRaycast(GrassRaycast& data, const std::string& path);

// Box filtered mip chain of RGBA8 texels, level after level starting with the image itself.
// levels : in, the most to make; out, how many were made (a dimension of 1 stays at 1)
std::vector<uint8_t> rgbaMipChain(const std::vector<uint8_t>& image, int width, int height, int depth, int& levels);

// --- CPU mirror of grass.frag.glsl, for the tests and the preview of tools/raycast ---

// Normal in two values in 0..1 : octahedral, +y in the middle. The upper half of the sphere is the inner
// diamond, the lower half the corners : no seam to wrap, texels filter and average like any other value
glm::vec2 encodeGrassNormal(glm::vec3 normal);
glm::vec3 decodeGrassNormal(glm::vec2 encoded);

// Filtered like the GPU sampler (linear, repeat) : the texel as the shader gets it, in 0..1
glm::vec4 sampleGrassOblique(const GrassRaycast& data, glm::vec2 q, float azimuth);
glm::vec4 sampleGrassTop(const GrassRaycast& data, glm::vec2 q);

struct GrassLookup {
    float height;       // of the hit on the view ray, 1 at the top of the grass
    float baked;        // height read in the textures (how deep the hit is in the grass)
    glm::vec3 normal;
    float id;           // of the blade
};

// Rays closer to the horizontal are looked up as if they crossed this many tiles
constexpr float grass_max_reach = 3.f;

// The view ray enters the top of the grass at q_top and goes down along the horizontal direction
// `along` (normalized); it crosses `reach` tiles before the ground (cotangent of its elevation).
// top_start : part of the way from 45 degrees to the vertical where the top down rays start to blend in
GrassLookup lookupGrass(const GrassRaycast& data, glm::vec2 q_top, glm::vec2 along, float reach, float top_start);

}

#endif
