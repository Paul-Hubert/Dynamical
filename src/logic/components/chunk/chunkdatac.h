#ifndef CHUNKDATAC_H
#define CHUNKDATAC_H

#include "chunkc.h"
#include "renderer/num_frames.h"

struct ChunkData {
    float values[chunk::num_values.x * chunk::num_values.y * chunk::num_values.z];
};

class StagedChunkData {
public:
    StagedChunkData() {
        for(int i = 0; i < data.size(); i++) {
            data[i] = nullptr;
            index[i] = 0;
        }
        sync = 0;
    }
    std::array<ChunkData*, NUM_FRAMES> data;
    std::array<int, NUM_FRAMES> index;
    int sync;
};

#endif
