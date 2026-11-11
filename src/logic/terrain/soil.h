#ifndef DY_SOIL_H
#define DY_SOIL_H

#include "logic/terrain/chunk.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace dy {

// What the ground is made of where it is not grass : patches of mud, of stones and of sand, each from its
// own Perlin noise anchored to the world. Baked once per chunk into a small 2D map that the terrain and
// the grass read (terrain.frag.glsl, grass.vert.glsl, grass.frag.glsl, docs/ground.md)
struct SoilPatchParams {
    float wavelength;   // metres, of the first octave of its noise
    float cover;        // part of the ground it covers, 0..1
};

struct SoilParams {
    int seed = 4242;
    int octaves = 3;            // each one `lacunarity` times finer and `gain` times weaker than the one before
    float lacunarity = 2.f;
    float gain = 0.5f;
    float edge_width = 8.f;     // metres over which the map goes from 0 to 1 across the edge of a patch
    float grass_margin = 6.f;   // metres from a patch where the grass starts to get shorter
    SoilPatchParams mud { 60.f, 0.05f };
    SoilPatchParams stones { 45.f, 0.07f };
    SoilPatchParams sand { 90.f, 0.06f };
};

namespace soil {

    // Channels of the maps, layers of the ground textures
    enum Kind : int { mud = 0, stones = 1, sand = 2, kinds = 3 };

    // One texel per column of terrain vertices, like the height map of the grass, and `border` more on
    // each side : texel (i, j) is at the origin of the chunk + (i - border, j - border) cubes. The shaders
    // read the map a few metres off (they bend the edges of the patches), up to a border out of the chunk,
    // where the neighbours have the same values
    constexpr int border = 1;
    constexpr int size = chunk::cubes + 1 + 2 * border;
    constexpr size_t num_texels = (size_t) size * size;
    constexpr int channels = 4;     // rgb : mud, stones, sand, a : height of the grass (grassHeight)

    // Octaves under two texels per wavelength cannot be in the map : they are left out
    int octaves(const SoilParams& params, const SoilPatchParams& patch);

    // The shaders bend the edge of a patch by less than this (soilWarp), and cut the grass there blade
    // by blade : the grass goes on this far into a patch, shorter and shorter
    constexpr float grass_inside = 4.5f;

    // How tall the grass is, as a part of what it is away from the patches, `inside` metres into the
    // closest one (negative out of it) : shorter from `margin` metres before the edge, none grass_inside after
    float grassHeight(float inside, float margin);

}

// Changes with the parameters and with the baker : a map baked with another key is stale
uint64_t soilKey(const SoilParams& params);

// Safe to call from several threads
class SoilField {
public:
    explicit SoilField(const SoilParams& params = {});
    ~SoilField();

    const SoilParams& params() const { return p; }
    uint64_t key() const { return soilKey(p); }

    // How far into its patches of mud, stones and sand a point of the world is, in metres : 0 on an edge,
    // negative outside. The noise is not a distance : this is its value over its slope, right next to
    // the edges, and only says inside or outside away from them (never more than max_distance)
    glm::vec3 distance(float x, float z) const;
    static constexpr float max_distance = 64.f;

    // The same as the maps hold it : 0.5 on the edge of a patch, 0 edge_width / 2 before it, 1 as far
    // after. w : height of the grass, 1 away from the patches
    glm::vec4 sample(float x, float z) const;

    // The noise value over which a kind is there : `cover` of the world is over it
    float threshold(soil::Kind kind) const;

    // Map of the chunks at chunk coordinates (chunk.x, *, chunk.y) : soil::num_texels RGBA texels, laid out [z][x]
    void bake(glm::ivec2 chunk, uint8_t* texels) const;
    std::vector<uint8_t> bake(glm::ivec2 chunk) const;

private:
    struct Noise;
    glm::vec4 texel(const glm::vec3& distance) const;

    SoilParams p;
    std::unique_ptr<Noise> noise;
};

// The map as the shaders read it (linear, clamped at its border) at a point of its chunk, in metres from
// the origin of the chunk
glm::vec4 sampleSoilMap(const uint8_t* texels, glm::vec2 local);

}

#endif
