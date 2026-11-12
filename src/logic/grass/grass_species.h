#ifndef DY_GRASS_SPECIES_H
#define DY_GRASS_SPECIES_H

#include "grass_height.h"
#include "grass_model.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace dy {

// Which plants grow where, and how tall (docs/grass/15-species.md) : every kind of tile (GrassKind) has two
// Perlin noises in octaves over the world. One says how much of it there is : the kinds whose noises are the
// highest at a place grow there together, each as dense as its noise is close to the best. The other says how
// tall it grows there. Small kinds (GrassKindInfo::small) get less of their noise where the tall kinds of the
// place grow tall, more where they stay short : in their shade or not.
// The shader reads the noises in a texture that repeats (grass.frag.glsl, species()); this is the same
// texture and the same choice on the CPU, and the top of the plants over a chunk, which the shell of the
// grass is drawn at (GrassTopField)
struct GrassSpeciesParams {
    float size = 40.f;      // metres : the first octave of the noises of how much, about the size of a patch. 0 : the meadow everywhere
    // How wide the patches blend into each other, 0 to 1 : a kind goes from all of its plants to none over
    // overlap x margin of its noise against the best kind's. 0 : one kind at a place, edges like lines
    float overlap = 0.8f;
    // Taken from the noise of the small kinds where the tall kinds of the place are at their tallest, given
    // where they are at their shortest. 0 : small and tall plants do not know each other
    float shade = 0.5f;
};

namespace grass_species {

    // The noise texture : RGBA8 signed, a noise per channel, `periods` first octaves across
    constexpr int size = 256;
    constexpr int periods = 4;
    constexpr int octaves = 3;     // each half as wide and 0.4 times as strong as the one before

    // How much of each kind. Kinds 0 to 3 read the texture as it is, 4 to 7 and 8 to 9 scaled and shifted :
    // the three do not repeat together
    constexpr float scales[3] = {1.f, 1.37f, 0.73f};
    constexpr glm::vec2 shifts[3] = {glm::vec2(0.f), glm::vec2(0.31f, 0.67f), glm::vec2(0.59f, 0.13f)};

    // How tall each kind : the same texture read wider (octaves of about 2 x size, size and size / 2 :
    // the terrain vertices, 4 m apart, follow them) and elsewhere
    constexpr float height_scales[3] = {0.53f, 0.61f, 0.47f};
    constexpr glm::vec2 height_shifts[3] = {glm::vec2(0.43f, 0.19f), glm::vec2(0.77f, 0.52f), glm::vec2(0.11f, 0.83f)};
    constexpr float height_contrast = 0.7f;     // of the noise around the middle height, before it is clamped to 0..1

    // GrassSpeciesParams::overlap of 1, in units of the noises
    constexpr float margin = 1.f;
    // Tall kinds within that much of the best tall one's noise count in how tall the grass of the place is
    constexpr float canopy_width = 0.5f;

    // A kind that is about to leave the layers for the next one (the fifth by its noise) loses its last
    // plants over that part of the blend width before it does : no line where they trade places
    constexpr float fade = 0.25f;

    // The last plants of a kind at the edge of its patch are that part of its height there, those of a kind
    // that keeps half of its plants or more are whole : few and small, then many and tall. The top of the
    // plants has no step where a tall kind comes in
    constexpr float stunted = 0.15f;

    // Kinds that grow together at a place : the layers of the grass
    constexpr int layers = 4;

}

// size x size x 4 signed bytes, [y][x], the same for a seed : it repeats in x and y
std::vector<int8_t> bakeSpeciesNoise(uint32_t seed = 4242);

struct GrassSpecies {
    // The layers of the grass, the first to the last : the kinds whose noises are the highest
    GrassKind kinds[grass_species::layers] = {GrassKind::Meadow, GrassKind::Meadow, GrassKind::Meadow, GrassKind::Meadow};
    // The part of the plants of each that are there, those of the lowest ids (GrassModel::ids) : 1 inside the
    // patch of the kind, a half where it meets the next one, less outside. A layer with no kind of its own is
    // the first kind again, more and more of it as the kind the layer had is further behind
    float keep[grass_species::layers] = {1.f, 1.f, 1.f, 1.f};
    // How tall each grows, as a part of the height of its kind (GrassModel::height) : 1 - amount to 1 by
    // the height noise of the kind, less at the edge of its patch (grass_species::stunted)
    float tall[grass_species::layers] = {1.f, 1.f, 1.f, 1.f};
    // How tall the tall kinds of the place grow, 0 their shortest to 1 their tallest : the shade over the small ones
    float canopy = 0.5f;
};

// amount : how much shorter a kind is where its height noise is lowest (RenderSettings::Grass::patch_amount).
// forced : that kind everywhere (grass_kind=...), < 0 for the patches
GrassSpecies grassSpeciesAt(const std::vector<int8_t>& noise, const GrassSpeciesParams& params, glm::vec2 world, float amount, int forced = -1);

struct GrassTopParams {
    GrassSpeciesParams species;
    float amount = 0.45f;           // RenderSettings::Grass::patch_amount
    int forced = -1;                // grass_kind=...
    // Height each kind was modelled at (GrassModel::height), as parts of the height of the grass
    std::array<float, grass_kinds> heights {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    GrassHeightParams height_map;   // how tall the meadow is without the species (species.size 0)
};

// Changes with the parameters and with the baker : a map baked with another key is stale
uint64_t grassTopKey(const GrassTopParams& params);

// The top of the plants : the tallest of the kinds that grow at a place. Baked per chunk in the map the
// shell of the grass reads (grass.vert.glsl, grass_height::size texels on the terrain vertices), so that the
// shell is right over the plants : low over clover, high over oat. A texel is the tallest plant within 2 m
// of its vertex : the shell goes straight from a vertex to the next, and plants come and go within less than
// that. The few that are still over it are drawn as tall as the shell. Safe to call from several threads
class GrassTopField {
public:
    explicit GrassTopField(const GrassTopParams& params = {});

    const GrassTopParams& params() const { return p; }
    uint64_t key() const { return grassTopKey(p); }

    // What a texel of 255 stands for, as a part of the height of the grass
    float top() const { return highest; }

    // Top of the plants at a point of the world, as a part of the height of the grass
    float sample(glm::vec2 world) const;

    // Map of the chunk at chunk coordinates (chunk.x, *, chunk.y) : grass_height::num_texels heights in
    // 0..255 (255 : top()), laid out [z][x]. The same on the edge two chunks share
    void bake(glm::ivec2 chunk, uint8_t* texels) const;
    std::vector<uint8_t> bake(glm::ivec2 chunk) const;

private:
    GrassTopParams p;
    std::vector<int8_t> noise;
    GrassHeightField meadow;
    float highest = 1.f;
};

}

#endif
