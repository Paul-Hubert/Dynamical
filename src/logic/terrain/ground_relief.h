#ifndef DY_GROUND_RELIEF_H
#define DY_GROUND_RELIEF_H

#include "logic/grass/grass_bake.h"

#include <cstdint>
#include <vector>

namespace dy {

// The relief of a ground texture (docs/ground-relief.md) : its height map is built as geometry, a tile of
// triangles repeated to infinity, and the rays of the grass are baked on it (logic/grass/grass_bake.h).
// terrain.frag.glsl looks the view ray up in them like grass.frag.glsl does : where it meets the stones,
// instead of where it meets the plane they are drawn on.
struct GroundReliefParams {
    int size = 256;         // of the height field and of the baked rays, along x and z
    // Azimuths of each elevation of the rays, the lowest first (GrassBakeParams::angles)
    std::array<int, grass_elevations> angles = {48, 40, 32, 24};
};

// heights : width x height values in 0..255, [y][x], of a texture that repeats. A vertex per texel of
// size x size (the heights are averaged down to that), y from 0 at the lowest height of the texture to
// max_height at the highest
GrassModel groundReliefModel(const uint8_t* heights, int width, int height, int size = 256, float max_height = 0.98f);

// Changes with the heights, the parameters and the baker
uint64_t groundReliefKey(const uint8_t* heights, int width, int height, const GroundReliefParams& params);

// Only the height of the hits means something in the result (the normal and the colour of a hit are read
// in the texture itself, where the ray met it)
GrassRaycast bakeGroundRelief(const uint8_t* heights, int width, int height, const GroundReliefParams& params = {}, unsigned threads = 0);

}

#endif
