#ifndef CHUNK_H
#define CHUNK_H

#include "renderer/vk.h"

#include <mutex>

class Chunk {
public:
    vk::Buffer triangles = nullptr;
    uint32_t triangles_offset = 0; // in array index count
    vk::Buffer indirect = nullptr;
    uint32_t indirect_offset = 0; // in array index count
    
    static std::mutex mutex;
};

class ChunkBuild {
public:
    int index = 0;
    vk::DescriptorSet set = nullptr;
};

#endif
