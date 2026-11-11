#ifndef DY_GRASS_SPECIES_H
#define DY_GRASS_SPECIES_H

#include "grass_model.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace dy {

// Which plants grow where (docs/grass/15-species.md) : every kind of tile (GrassKind) has its own Perlin
// noise in octaves over the world, and the kind whose noise is the highest at a place grows there, in
// patches. Tall kinds are told from small ones (GrassKindInfo::small) : the small ones only grow where
// the grass is short (the height map of the grass, GrassHeightField), the tall ones where it is not.
// The shader reads the noises in a texture that repeats (grass.frag.glsl, species()); this is the same
// texture and the same choice on the CPU.
struct GrassSpeciesParams {
    float size = 40.f;      // metres : the first octave of the noises, about the size of a patch. 0 : the meadow everywhere
    float low = 0.4f;       // small plants where the grass is under that part of the way from its shortest to its tallest
    // How much the patches overlap, 0 to 1 : kinds whose noise is within overlap x margin of the best one grow
    // together, and small and tall plants both grow over overlap x band of the heights around `low`. 0 : one
    // kind at a place, small plants on one side of a line
    float overlap = 0.5f;
};

namespace grass_species {

    // The noise texture : RGBA8 signed, a noise per channel, `periods` first octaves across
    constexpr int size = 256;
    constexpr int periods = 4;
    constexpr int octaves = 3;     // each half as wide and 0.4 times as strong as the one before

    // Kinds 0 to 3 read the texture as it is, 4 to 7 and 8 to 9 scaled and shifted : the three do not
    // repeat together
    constexpr float scales[3] = {1.f, 1.37f, 0.73f};
    constexpr glm::vec2 shifts[3] = {glm::vec2(0.f), glm::vec2(0.31f, 0.67f), glm::vec2(0.59f, 0.13f)};

    // GrassSpeciesParams::overlap of 1 : in units of the noises, and of the height of the grass (0 to 1)
    constexpr float margin = 0.7f;
    constexpr float band = 0.3f;

}

// size x size x 4 signed bytes, [y][x], the same for a seed : it repeats in x and y
std::vector<int8_t> bakeSpeciesNoise(uint32_t seed = 4242);

struct GrassSpecies {
    // The tall layers of the grass, the first to the third : the kind whose noise is the highest, then those
    // close enough to it to grow with it (the first kind again where there is none)
    GrassKind kinds[3] = {GrassKind::Meadow, GrassKind::Meadow, GrassKind::Meadow};
    // Under them : short meadow grass (-1), a small kind where small and tall plants both grow (its index),
    // nothing under small plants (-2)
    int under = -1;
};

// tallness : height of the grass at the place, 0 its shortest and 1 its tallest (GrassHeightField::sample)
GrassSpecies grassSpeciesAt(const std::vector<int8_t>& noise, const GrassSpeciesParams& params, glm::vec2 world, float tallness);

}

#endif
