#ifndef DY_CHUNK_H
#define DY_CHUNK_H

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace dy {

// Marching cubes chunks : a density field sampled on a regular grid, > 0 is solid.
// Ported from the 2019 voxel engine (single LOD, raw storage instead of the sparse octree).
namespace chunk {

    constexpr int cube_size = 4;                        // world units per marching cube
    constexpr int cubes = 64;                           // cubes per chunk side
    constexpr int length = cube_size * cubes;           // world units per chunk side
    constexpr int border = 1;                           // extra samples for gradient normals
    constexpr int values = cubes + 1 + 2 * border;      // density samples per chunk side
    constexpr size_t num_values = (size_t) values * values * values;

    // Sample layout expected by the marching cubes shader : [x][z][y]
    inline size_t index(int x, int y, int z) {
        return ((size_t) x * values + (size_t) z) * values + (size_t) y;
    }

    // World position of sample (x, y, z) of the chunk at chunk coordinates pos
    inline glm::vec3 samplePosition(glm::ivec3 pos, int x, int y, int z) {
        return glm::vec3(pos * length + (glm::ivec3(x, y, z) - border) * cube_size);
    }

}

class ChunkC {
public:
    ChunkC(glm::ivec3 pos) : pos(pos) {}
    glm::ivec3 pos;

    glm::vec3 origin() const {
        return glm::vec3(pos * chunk::length);
    }
    glm::vec3 center() const {
        return origin() + glm::vec3(chunk::length / 2.f);
    }
};

// Density samples of a chunk crossing the surface
class ChunkVolume {
public:
    std::vector<float> values;
};

// Every sample is on the same side of the surface : nothing to mesh
class ChunkEmpty {
public:
    float mean;
};

// Lifecycle tags
struct ChunkGenerating {};     // density being computed on a worker thread
struct ChunkGenerated {};      // ChunkVolume or ChunkEmpty present
struct ChunkMeshPending {};    // waits for a marching cubes dispatch
struct ChunkDestroying {};     // out of view distance, destroyed once the GPU and workers are done with it

struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const {
        return ((size_t) (uint32_t) v.x * 73856093u) ^ ((size_t) (uint32_t) v.y * 19349663u) ^ ((size_t) (uint32_t) v.z * 83492791u);
    }
};

// Context variable : chunk coordinates to chunk entity
class ChunkMap {
public:
    std::unordered_map<glm::ivec3, entt::entity, IVec3Hash> chunks;

    entt::entity get(glm::ivec3 pos) const {
        auto it = chunks.find(pos);
        return it != chunks.end() ? it->second : entt::null;
    }
};

}

#endif
