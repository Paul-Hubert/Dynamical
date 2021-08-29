#include "system_list.h"

#include <taskflow/taskflow.hpp>

#include "logic/components/camerac.h"

#include "logic/map/map_manager.h"


void ChunkGenerationSys::preinit() {
    
}

void ChunkGenerationSys::init() {
    
}

void ChunkGenerationSys::tick(float dt) { // to be parallelized
    auto& map = reg.ctx<MapManager>();
    auto& camera = reg.ctx<CameraC>();
    
    auto corner_pos = map.getChunkPos(camera.corner)-2;
    auto end_pos = map.getChunkPos(camera.corner + camera.size)+2;
    
    for(int x = corner_pos.x; x <= end_pos.x; x++) {
        for(int y = corner_pos.y; y <= end_pos.y; y++) {
            
            auto pos = glm::ivec2(x, y);
            
            map.generateChunk(map.getChunkPos(pos));
            
        }
    }
    
    
}

void ChunkGenerationSys::finish() {
    
}
