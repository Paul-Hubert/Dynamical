#ifndef CHUNKC_H
#define CHUNKC_H

#include "glm/glm.hpp"
#include "util/util.h"

namespace chunk {

    constexpr int base_cube_size = 4;
    constexpr int base_length = base_cube_size*8*8;
    constexpr int base_height = base_length;
    constexpr glm::ivec3 base_size(base_length, base_height, base_length);
    constexpr int lod_multiplier = 2;
    constexpr glm::ivec3 num_cubes = base_size/base_cube_size;
    constexpr int border = 1;
    constexpr glm::ivec3 num_values = num_cubes+1+2*border;
    constexpr int max_lod = 0;
    constexpr int max_mul = Util::c_pow(lod_multiplier, max_lod);
    constexpr glm::ivec3 max_num_values = num_values*max_mul;
    
}

class ChunkC {
public:
    ChunkC() {}
    ChunkC(glm::ivec3 pos, int lod) : pos(pos), lod(lod) {}
    glm::ivec3 pos;
    int lod;
    
    glm::ivec3 getPosition() {
        return pos * chunk::base_size;
    }
    
    glm::ivec3 getSize() {
        return (int) std::pow(chunk::lod_multiplier, lod) * chunk::base_size;
    }
    
    int getCubeSize() {
        return std::pow(chunk::lod_multiplier, lod) * chunk::base_cube_size;
    }
    
    int getLOD() {
        return std::pow(chunk::lod_multiplier, lod);
    }
    
};

class GlobalChunkC : public ChunkC {
public:
    GlobalChunkC() {}
    GlobalChunkC(glm::ivec3 pos, int lod) : ChunkC(pos, lod) {}
};
class OuterGlobalChunk {
public:
    OuterGlobalChunk(entt::entity entity) : entity(entity) {}
    entt::entity entity;
};

#endif
