#ifndef DY_TERRAIN_GENERATOR_H
#define DY_TERRAIN_GENERATOR_H

#include <glm/glm.hpp>

#include <vector>

#include "FastNoise/FastNoise.h"

namespace dy {

// Density function of the marching cubes world : a FastNoise2 heightfield plus 3D noise for overhangs.
// Context variable, safe to call from several threads.
class TerrainGenerator {
public:
    TerrainGenerator(int seed = 1337);

    // Fills values (chunk::num_values samples, [x][z][y] layout) for the chunk at chunk coordinates pos.
    // Returns false if every sample has the same sign (no surface), with mean set.
    bool generate(glm::ivec3 pos, std::vector<float>& values, float& mean) const;

    // Samples any box of the density grid : start is a global sample index (world position / cube size),
    // values are laid out [x][z][y] like chunk volumes. Chunks and physics colliders get identical values.
    void sample(glm::ivec3 start, glm::ivec3 count, std::vector<float>& values) const;

    // Height of the heightfield part of the surface, good enough to place things on the ground
    float surfaceHeight(float x, float z) const;

    int seed;
    float base_level = 60.f;
    float height_amplitude = 90.f;
    float height_frequency = 1.f / 700.f;
    float overhang_amplitude = 14.f;
    float overhang_frequency = 1.f / 90.f;

private:
    FastNoise::SmartNode<> surface;
    FastNoise::SmartNode<> overhang;
};

}

#endif
