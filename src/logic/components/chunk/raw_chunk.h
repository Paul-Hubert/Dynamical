#ifndef RAW_CHUNK_H
#define RAW_CHUNK_H

#include "global_chunk_data.h"

class RawChunk {
public:
    
    RawChunk() {
        
    }
    
    void get(GlobalChunkData& data) {
        data = this->data;
    }
    
    void set(GlobalChunkData& cd) {
        data = cd;
    }
    
    float get(int x, int y, int z) {
        return data.data[x * chunk::max_num_values.y * chunk::max_num_values.z + z * chunk::max_num_values.y + y];
    }
    
private:
    GlobalChunkData data;
    
};

#endif
