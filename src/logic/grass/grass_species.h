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
    float size = 24.f;      // metres : the first octave of the noises, about the size of a patch. 0 : the meadow everywhere
    float low = 0.4f;       // small plants where the grass is under that part of the way from its shortest to its tallest
};

namespace grass_species {

    // The noise texture : RGBA8 signed, a noise per channel, `periods` first octaves across
    constexpr int size = 256;
    constexpr int periods = 4;
    constexpr int octaves = 4;

    // Kinds 0 to 3 read the texture as it is, 4 to 7 and 8 to 9 scaled and shifted : the three do not
    // repeat together
    constexpr float scales[3] = {1.f, 1.37f, 0.73f};
    constexpr glm::vec2 shifts[3] = {glm::vec2(0.f), glm::vec2(0.31f, 0.67f), glm::vec2(0.59f, 0.13f)};

}

// size x size x 4 signed bytes, [y][x], the same for a seed : it repeats in x and y
std::vector<int8_t> bakeSpeciesNoise(uint32_t seed = 4242);

struct GrassSpecies {
    GrassKind primary = GrassKind::Meadow;      // the first tall layer of the grass
    GrassKind secondary = GrassKind::Meadow;    // the second : the next kind of the place, in between
    bool small = false;                         // the grass is short here : small plants
};

// tallness : height of the grass at the place, 0 its shortest and 1 its tallest (GrassHeightField::sample)
GrassSpecies grassSpeciesAt(const std::vector<int8_t>& noise, const GrassSpeciesParams& params, glm::vec2 world, float tallness);

}

#endif
