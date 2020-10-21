#ifndef GLOBAL_CHUNK_H
#define GLOBAL_CHUNK_H

#include "entt/entt.hpp"

#include "chunkc.h"

class GlobalChunkData {
public:
    GlobalChunkData() : data(chunk::max_num_values.x * chunk::max_num_values.y * chunk::max_num_values.z) {};
    std::vector<float> data;
    
};

class GlobalChunkEmpty {
public:
    GlobalChunkEmpty(float mean) : mean(mean) {} 
    float mean;
};

#endif
