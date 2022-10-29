#include "system_list.h"

#include <taskflow/taskflow.hpp>

#include "logic/components/camera.h"

#include "logic/map/map_manager.h"

#include <util/log.h>

using namespace dy;

void ChunkGenerationSys::preinit() {
    
}

void ChunkGenerationSys::init() {
    
}

void ChunkGenerationSys::tick(float dt) { // to be parallelized
    
    OPTICK_EVENT();
    
    auto& map = reg.ctx<MapManager>();
    auto& camera = reg.ctx<Camera>();

    auto corner_rpos = camera.fromScreenSpace(glm::vec2()) - 10.f;
    auto corner_pos = map.getChunkPos(corner_rpos);
    auto end_rpos = camera.fromScreenSpace(camera.getScreenSize()) + 10.f;
    auto end_pos = map.getChunkPos(end_rpos);
    end_pos.y++;
    
    for(int x = corner_pos.x; x <= end_pos.x; x++) {
        for(int y = corner_pos.y; y <= end_pos.y; y++) {
            
            auto pos = glm::ivec2(x, y);
            map.generateChunk(pos);
            
        }
    }
    
    /*
    tf::Executor& ex = reg.ctx<tf::Executor>();
    
    tf::Taskflow tf;
    
    //dy::log() << chunks.size() << "\n";
    
    tf::Task chunk_gen = tf.for_each(chunks.begin(), chunks.end(), [&](glm::ivec2 pos){
        
    });
    
    ex.run(tf).wait();
    */
    
}

void ChunkGenerationSys::finish() {
    
}
