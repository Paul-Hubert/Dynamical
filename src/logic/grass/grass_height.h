#ifndef DY_GRASS_HEIGHT_H
#define DY_GRASS_HEIGHT_H

#include "logic/terrain/chunk.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace dy {

// How tall the grass is across the world : Perlin noise in octaves, low frequency, anchored to the world.
// Baked once per chunk into a small 2D map that the grass shell reads (grass.vert.glsl,
// docs/grass/11-height-map.md)
struct GrassHeightParams {
    int seed = 1337;
    float wavelength = 96.f;    // metres, of the first octave
    int octaves = 4;            // each one `lacunarity` times finer and `gain` times weaker than the one before
    float lacunarity = 2.f;
    float gain = 0.5f;
    float contrast = 1.6f;      // of the noise around the middle height, before it is clamped to 0..1
};

namespace grass_height {

    // One texel per column of terrain vertices : texel (i, j) is at the origin of the chunk + (i, j) cubes.
    // The last row and column are the first ones of the next chunks, so the map is continuous across them
    constexpr int size = chunk::cubes + 1;
    constexpr size_t num_texels = (size_t) size * size;

    // Octaves under two texels per wavelength cannot be in the map : they are left out
    int octaves(const GrassHeightParams& params);

}

// Changes with the parameters and with the baker : a map baked with another key is stale
uint64_t grassHeightKey(const GrassHeightParams& params);

// Safe to call from several threads
class GrassHeightField {
public:
    explicit GrassHeightField(const GrassHeightParams& params = {});
    ~GrassHeightField();

    const GrassHeightParams& params() const { return p; }
    uint64_t key() const { return grassHeightKey(p); }

    // Height of the grass at a point of the world, as a part of the tallest : 0 the shortest, 1 the tallest
    float sample(float x, float z) const;

    // Map of the chunks at chunk coordinates (chunk.x, *, chunk.y) : grass_height::num_texels heights
    // in 0..255, laid out [z][x]
    void bake(glm::ivec2 chunk, uint8_t* texels) const;
    std::vector<uint8_t> bake(glm::ivec2 chunk) const;

private:
    struct Noise;
    GrassHeightParams p;
    std::unique_ptr<Noise> noise;
};

// The map as the shader reads it (linear, clamped) at a point of its chunk, in metres from the origin
// of the chunk : 0..1
float sampleGrassHeightMap(const uint8_t* texels, glm::vec2 local);

}

#endif
