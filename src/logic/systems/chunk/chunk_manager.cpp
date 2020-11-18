#include "logic/systems/system_list.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

#include <util/util.h>
#include <logic/components/chunk/chunkc.h>
#include <logic/components/chunk/chunk_map.h>
#include <logic/components/camerac.h>
#include <renderer/camera.h>
#include "logic/components/chunk/global_chunk_data.h"
#include "logic/components/positionc.h"

#include <util/entt_util.h>

void ChunkManagerSys::preinit() {

}

void ChunkManagerSys::init() {
    reg.set<ChunkMap>();
}

void ChunkManagerSys::tick() {
    
    ChunkMap& map = reg.ctx<ChunkMap>();
    GlobalChunkMap& global_map = reg.ctx<GlobalChunkMap>();
    
    auto& pos = reg.get<PositionC>(reg.ctx<Util::Entity<"player"_hs>>()).pos;
    
    ChunkC g_chunk;
    g_chunk.lod = chunk::max_mul;
    const int render_chunks = render_distance / (g_chunk.getSize().x)+1;
    reg.view<ChunkC>().each([&](entt::entity entity, ChunkC& chunk) {
        if(glm::distance2(glm::vec3(chunk.getPosition()), pos) > Util::sq(render_distance + chunk.getSize().x*2)) {
            map.set(entt::null, chunk.pos.x, chunk.pos.y, chunk.pos.z, chunk::max_mul/2);
            if(!reg.has<entt::tag<"destroying"_hs>>(entity)) reg.emplace<entt::tag<"destroying"_hs>>(entity);
        }
    });
    
    for(int x = -render_chunks; x<=render_chunks; x++) {
        for(int z = -render_chunks; z<=render_chunks; z++) {
            for(int y = -render_chunks; y<=render_chunks; y++) {
                
                g_chunk.pos = glm::vec3(x + std::round(pos.x/(chunk::max_mul*chunk::base_length)), y + std::round(pos.y/(chunk::max_mul*chunk::base_length)), z + std::round(pos.z/(chunk::max_mul*chunk::base_length))) * (float) chunk::max_mul;
                
                if(map.get(g_chunk.pos.x, g_chunk.pos.y, g_chunk.pos.z, chunk::max_mul/2) == entt::null && glm::distance2(glm::vec3(g_chunk.getPosition()), pos) < Util::sq(render_distance + g_chunk.getSize().x)) {
                    
                    entt::entity global_entity = global_map.get(g_chunk.pos.x, g_chunk.pos.y, g_chunk.pos.z, chunk::max_mul/2);
                    if(global_entity == entt::null) {
                        global_entity = reg.create();
                        reg.emplace<GlobalChunkC>(global_entity, (g_chunk.pos/chunk::max_mul)*chunk::max_mul, chunk::max_lod);
                        global_map.set(global_entity, g_chunk.pos.x, g_chunk.pos.y, g_chunk.pos.z, chunk::max_mul/2);
                    }
                    
                    if(!reg.has<entt::tag<"loading"_hs>>(global_entity) && !reg.has<entt::tag<"loaded"_hs>>(global_entity))
                        reg.emplace<entt::tag<"loading"_hs>>(global_entity);
                    
                    ChunkC chunk;
                    chunk.lod = chunk::max_lod;
                    chunk.pos = g_chunk.pos;
                    int mul = chunk.getLOD();
                    
                    auto chunk_entity = reg.create();
                    reg.emplace<ChunkC>(chunk_entity, chunk);
                    map.set(chunk_entity, chunk.pos.x, chunk.pos.y, chunk.pos.z, mul/2);
                    
                    reg.emplace<OuterGlobalChunk>(chunk_entity, global_entity);
                    if(!reg.has<GlobalChunkEmpty>(global_entity))
                        reg.emplace<entt::tag<"preparing"_hs>>(chunk_entity);
                    
                    
                }
                
            }
        }
    }
    
}

void ChunkManagerSys::finish() {

}