#ifndef PHYSICS_H
#define PHYSICS_H

#include "system.h"

#include <memory>

#include "btBulletDynamicsCommon.h"

class PhysicsSys : public System {
public:
    PhysicsSys(entt::registry& reg);
    void preinit() override;
    void init() override;
    void tick(float dt) override;
    ~PhysicsSys();
    const char* name() override {return "Physics";};
    
    std::unique_ptr<btDefaultCollisionConfiguration> collisionConfiguration;
    std::unique_ptr<btCollisionDispatcher> dispatcher;
    std::unique_ptr<btDbvtBroadphase> overlappingPairCache;
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
    std::unique_ptr<btDiscreteDynamicsWorld> world;

};

#endif
