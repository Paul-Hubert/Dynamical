#ifndef DY_PROBES_H
#define DY_PROBES_H

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "logic/terrain/chunk.h"

namespace dy {

// Irradiance probes of the lighting (docs/lighting.md), the part that needs no GPU : which probes of a chunk
// are traced, the light a probe stores, where the bricks of the chunks are in the atlas. Mirrored by
// include/probes.glsl and probe_update.rt.comp.glsl
namespace probes {

    constexpr int size = chunk::cubes + 1;              // probes a side of a brick : one on every density sample of the chunk
    constexpr size_t count = (size_t) size * size * size;
    constexpr float spacing = (float) chunk::cube_size; // metres

    // x first, as the texels of the brick in its 3D texture
    inline uint32_t index(int x, int y, int z) {
        return (uint32_t) ((z * size + y) * size + x);
    }

    enum Class : uint8_t {
        Rock = 0,       // in the ground, far from the air : never read
        Fill = 1,       // in the ground next to the air : the mean of its neighbours in the air, so that filtering does not pull black into the ground's light
        Active = 2,     // in the air within reach of the ground or of a plant : traced
        Open = 3,       // the rest of the air : the sky and the unobstructed sun, without rays
    };

    constexpr float reach = 8.f;    // metres from the ground or from the bounds of a plant where probes are traced

}

// A plant, for the probes : a box around it
struct PlantBounds {
    glm::vec3 base = glm::vec3(0.f);    // on the ground
    float radius = 0.f;
    float height = 0.f;
};

struct ProbeClasses {
    std::vector<uint8_t> classes;       // probes::Class of each probe, probes::count of them
    std::vector<uint32_t> active;       // indices of the probes to trace
    std::vector<uint32_t> fill;         // and of those to fill from their neighbours
};

// densities : the samples of the chunk (ChunkVolume::values, > 0 in the ground). origin : of the chunk in the world
ProbeClasses classifyProbes(const std::vector<float>& densities, glm::vec3 origin, const std::vector<PlantBounds>& plants);

// The light arriving at a probe from all around, as two bands of spherical harmonics per colour, stored
// folded with the cosine of a surface : x is the mean radiance, yzw twice the mean of radiance times
// direction, and what a white surface of normal n shows is x + dot(yzw, n)
struct ProbeLight {
    glm::vec4 r = glm::vec4(0.f), g = glm::vec4(0.f), b = glm::vec4(0.f);

    // One ray of `rays` spread evenly over the sphere
    void add(glm::vec3 direction, glm::vec3 radiance, int rays) {
        float w = 1.f / (float) rays;
        r += glm::vec4(1.f, 2.f * direction) * (radiance.r * w);
        g += glm::vec4(1.f, 2.f * direction) * (radiance.g * w);
        b += glm::vec4(1.f, 2.f * direction) * (radiance.b * w);
    }

    glm::vec3 irradiance(glm::vec3 normal) const {
        glm::vec4 n(1.f, normal);
        return glm::max(glm::vec3(glm::dot(r, n), glm::dot(g, n), glm::dot(b, n)), glm::vec3(0.f));
    }
};

// Direction i of n spread evenly over the sphere (a spherical Fibonacci set)
glm::vec3 fibonacciDirection(int i, int n);

// Where the bricks of the chunks are : in pages of 8 (2 x 2 x 2 bricks, 130³ texels), and found from a world
// position through a small 3D table of chunk coordinates that wraps (16³ : a chunk and the one 16 chunks
// further share a cell, only one of them has a brick)
class ProbeBricks {
public:
    static constexpr int page_bricks = 8;
    static constexpr int page_side = 2;
    static constexpr int wrap = 16;

    explicit ProbeBricks(int max_pages) : max_bricks(max_pages * page_bricks), table((size_t) wrap * wrap * wrap, 0) {}

    // The brick of a chunk, a new one if it has none : -1 when there is no room (every page full, or the
    // cell of the table taken by another chunk)
    int allocate(glm::ivec3 chunk);
    void release(glm::ivec3 chunk);
    int find(glm::ivec3 chunk) const;

    // Pages that hold a brick, or held one : they are never taken back
    int pages() const { return ((int) bricks.size() + page_bricks - 1) / page_bricks; }
    int used() const { return (int) owners.size(); }

    static int page(int brick) { return brick / page_bricks; }
    // Texel of the first probe of a brick in its page
    static glm::ivec3 offset(int brick) {
        int slot = brick % page_bricks;
        return glm::ivec3(slot & 1, (slot >> 1) & 1, slot >> 2) * probes::size;
    }
    static size_t cell(glm::ivec3 chunk) {
        glm::ivec3 w = chunk & (wrap - 1);
        return ((size_t) w.z * wrap + w.y) * wrap + w.x;
    }

    // Brick + 1 of each cell, 0 : none. Texel (x, y, z) of the table is cell x + wrap * (y + wrap * z)
    const std::vector<uint16_t>& indirection() const { return table; }

private:
    int max_bricks;
    std::vector<uint8_t> bricks;        // taken or free, by index
    std::unordered_map<glm::ivec3, int, IVec3Hash> owners;
    std::vector<uint16_t> table;
};

}

#endif
