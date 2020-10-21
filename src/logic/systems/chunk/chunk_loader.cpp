#include "logic/systems/system_list.h"

#include "logic/components/chunk/chunk_map.h"

void ChunkLoaderSys::init() {
    reg.set<GlobalChunkMap>();
}

void ChunkLoaderSys::tick() {
    
}

