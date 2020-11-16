#include "logic/systems/system_list.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

#include "logic/components/chunk/chunkc.h"
#include "util/util.h"

#include "logic/components/chunk/chunkdatac.h"
#include "logic/components/chunk/chunk_map.h"
#include "logic/components/chunk/global_chunk_data.h"
#include "logic/components/chunk/stored_chunk.h"

#include "logic/components/camerac.h"
#include "renderer/camera.h"
#include "renderer/marching_cubes/chunk.h"
#include "renderer/marching_cubes/marching_cubes.h"
#include "logic/components/renderinfo.h"

void ChunkSys::init() {
    
}

void ChunkSys::tick() {
    
    auto& cd = reg.ctx<StagedChunkData>();
    int& ri = cd.sync;
    ri = (ri+1)%NUM_FRAMES;
    
    
    auto view = reg.view<ChunkC, OuterGlobalChunk, entt::tag<"preparing"_hs>>();
    for(entt::entity entity : view) {
        
        entt::entity outer = view.get<OuterGlobalChunk>(entity).entity;
        
        if(reg.has<entt::tag<"loaded"_hs>>(outer)) {
            
            if(cd.index[ri] >= max_per_frame) break;
            
            if(reg.has<StoredChunk>(outer)) {
                reg.emplace<entt::tag<"modified"_hs>>(entity);
                ChunkBuild& cb = reg.emplace<ChunkBuild>(entity);
                cb.index = cd.index[ri];
                cd.index[ri]++;
                
                auto& global_chunk = reg.get<StoredChunk>(outer);
                
                GlobalChunkData chunk_data;
                global_chunk.get(chunk_data);
                ChunkData* chunkData = cd.data[ri] + cb.index;
                ChunkC& chunk = view.get<ChunkC>(entity);
                int mul = chunk.getLOD();
                
                int index = 0;
                for(int x = 0; x < chunk::num_values.x; x++) {
                    for(int z = 0; z < chunk::num_values.z; z++) {
                        for(int y = 0; y < chunk::num_values.y; y++) {
                            glm::ivec3 coords = (glm::ivec3(x, y, z) - chunk::border) * mul + chunk::border * chunk::max_mul;
                            chunkData->values[index++] = chunk_data.data[coords.x * chunk::max_num_values.y * chunk::max_num_values.z + coords.z * chunk::max_num_values.y + coords.y];
                        }
                    }
                }
                
                reg.remove<entt::tag<"preparing"_hs>>(entity);

            }
            
        }
        
    }
    
}
