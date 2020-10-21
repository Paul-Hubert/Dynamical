#include "physics.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

#include "logic/components/physicsc.h"
#include "logic/components/positionc.h"
#include "logic/components/chunk/chunkc.h"
#include "logic/components/chunk/chunk_map.h"
#include "logic/components/chunk/stored_chunk.h"

void PhysicsSys::preinit() {
    
}

void PhysicsSys::init() {
    
}

void PhysicsSys::tick() {
    
    reg.view<PhysicsC, PositionC, entt::tag<"forces"_hs>>().each([](PhysicsC& physics, PositionC& pos, auto) {
        
        physics.v.y -= 9.81f / 60.f;
        
    });
    
    reg.view<PhysicsC, PositionC>().each([this](auto entity, PhysicsC& physics, PositionC& pos) {
        
        pos.pos += physics.v / 60.f;
        
        glm::vec3 coords = glm::floor(pos.pos / glm::vec3(chunk::base_size * chunk::max_mul));
        
        glm::vec3 in = (pos.pos / glm::vec3(chunk::base_size * chunk::max_mul) - coords) * glm::vec3(chunk::num_cubes + 1) + (float) chunk::border;
        
        //Util::log(Util::TRACE) << "(" << in.x << ", " << in.y << ", " << in.z << ")" << std::endl;
        
        auto& map = reg.ctx<GlobalChunkMap>();
        entt::entity chunk = map.get(coords.x, coords.y, coords.z, chunk::max_lod);
        
        if(chunk == entt::null) {
            chunk = reg.create();
            reg.assign<GlobalChunkC>(chunk, coords, chunk::max_lod);
            reg.assign<entt::tag<"loading"_hs>>(chunk);
            map.set(chunk, coords.x, coords.y, coords.z, chunk::max_mul/2);
            return;
        }
        
        if(!reg.has<entt::tag<"loaded"_hs>>(chunk)) {
            if(!reg.has<entt::tag<"loading"_hs>>(chunk)) reg.assign<entt::tag<"loading"_hs>>(chunk);
            return;
        }
        
        if(!reg.has<StoredChunk>(chunk)) {
            if(reg.get<GlobalChunkEmpty>(chunk).mean < 0) return;
            Util::log(Util::debug) << "object in an empty sparse chunk" << std::endl;
            return;
        }
        
        auto& sc = reg.get<StoredChunk>(chunk);
        
        auto ini = glm::ivec3(glm::floor(in));
        in -= ini;
        
        float d[8];
        for(int x = 0; x<2; x++) {
            for(int y = 0; y<2; y++) {
                for(int z = 0; z<2; z++) {
                    d[x*4+y*2+z] = sc.get(ini.x + x, ini.y + y, ini.z + z);
                }
            }
        }
        
        auto get = [](int x, int y, int z) {return x*4+y*2+z;};
        
        float density = Util::lerp(
            Util::lerp(
                Util::lerp(d[get(0, 0, 0)], d[get(1, 0, 0)], in.x),
                Util::lerp(d[get(0, 1, 0)], d[get(1, 1, 0)], in.x),
                in.y
            ),
            Util::lerp(
                Util::lerp(d[get(0, 0, 1)], d[get(1, 0, 1)], in.x),
                Util::lerp(d[get(0, 1, 1)], d[get(1, 1, 1)], in.x),
                in.y
            ),
            in.z
        );
        
        if(density < 0) return;
        
        
        auto derivative = (glm::vec3(
            d[get(0, 0, 0)] - d[get(1, 0, 0)],
            d[get(0, 0, 0)] - d[get(0, 1, 0)],
            d[get(0, 0, 0)] - d[get(0, 0, 1)]
        ));
        
        float length = glm::length2(derivative);
        if(length == 0.) {derivative = glm::vec3(0, 1, 0);}
        else {derivative /= std::sqrt(length);}
        
        pos.pos += (derivative * density); // if sdf
        physics.v = glm::vec3(0, 0, 0);
        
    });
    
    
    
}
