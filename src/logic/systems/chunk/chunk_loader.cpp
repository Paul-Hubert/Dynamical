#include "logic/systems/system_list.h"

#include "logic/components/chunk/chunk_map.h"

void ChunkLoaderSys::preinit() {

}

void ChunkLoaderSys::init() {
    reg.set<GlobalChunkMap>();
}

void ChunkLoaderSys::tick() {
    
}

void ChunkLoaderSys::finish() {

}
