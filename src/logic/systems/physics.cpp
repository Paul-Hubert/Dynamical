#include "physics.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

#include "logic/components/physicsc.h"
#include "logic/components/positionc.h"

void PhysicsSys::preinit() {
    
}

void PhysicsSys::init() {
    
}

void PhysicsSys::tick() {

    auto view = reg.view<PhysicsC, PositionC, entt::tag<"forces"_hs>>();
    for(auto entity : view) {
        PhysicsC& physics = view.get<PhysicsC>(entity);
        physics.v.y -= 9.81f / 60.f;
    }
    
    reg.view<PhysicsC, PositionC>().each([this](auto entity, PhysicsC& physics, PositionC& pos) {
        
        pos.pos += physics.v / 60.f;
        
    });
    
    
    
}
