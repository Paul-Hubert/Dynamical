#ifndef DY_GRASS_TEXTURES_H
#define DY_GRASS_TEXTURES_H

#include "grass_bake.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dy {

// The baked rays as the grass pass uploads them (grass.frag.glsl). A pixel reads the height of the hit of
// every layer, more than once, and the normal and the blade of the closest hit only : the height is a
// texture of its own, one byte a texel where the shader reads the most instead of four, half a byte in
// blocks. Normals, ids and colours are three more textures, read once.

// One channel of 4 x 4 texels ([y * 4 + x]) in 8 bytes, BC4 : two end values, and for each texel one of
// the values spread between them (or 0 or 255, when the first end is not over the second)
void encodeBC4(const uint8_t texels[16], uint8_t block[8]);
void decodeBC4(const uint8_t block[8], uint8_t texels[16]);

// The colours of 4 x 4 RGBA8 texels in 8 bytes, BC1 : two colours of 5, 6 and 5 bits, and for each texel
// one of the four colours from one to the other. Alpha is not kept
void encodeBC1(const uint8_t texels[64], uint8_t block[8]);
void decodeBC1(const uint8_t block[8], uint8_t texels[64]);

struct GrassTexture {
    std::vector<uint8_t> data;          // level after level, the largest first
    std::vector<size_t> level_sizes;    // in bytes
};

struct GrassTextures {
    int size = 0;           // of the first level, along x and z
    int depth = 1;          // layers of the oblique rays (an azimuth of an elevation each), 1 for the rays from the top
    bool compressed = false;
    GrassTexture height;    // R8, or BC4
    GrassTexture normal;    // RG8, or BC5 (encodeGrassNormal)
    GrassTexture id;        // R8, or BC4

    int levels() const { return int(height.level_sizes.size()); }
    size_t bytes() const { return height.data.size() + normal.data.size() + id.data.size(); }
};

// image : RGBA8 texels of a GrassRaycast, [depth][z][x] : layers of a 2D array, every level keeps them
// all. The mips are those of rgbaMipChain, max_levels at most. compressed : blocks of 4 x 4 texels, layer
// after layer; a level that is not whole blocks wide is filled with the texels of its other side (the
// tile repeats)
GrassTextures makeGrassTextures(const std::vector<uint8_t>& image, int size, int depth, int max_levels, bool compressed);

// The colour map of the same rays (GrassRaycast::oblique_colour, top_colour), a texture of its own that the
// closest hit reads once : RGBA8, or BC1, both sRGB. Levels and blocks as above
GrassTexture makeGrassColour(const std::vector<uint8_t>& image, int size, int depth, int max_levels, bool compressed);

// The rays as the shader gets them back from the blocks (first level) : what the compression costs, for
// the tests and raycast --preview
GrassRaycast compressedGrassRaycast(const GrassRaycast& data);

}

#endif
