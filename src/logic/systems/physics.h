#ifndef PHYSICS_H
#define PHYSICS_H

#include "system.h"

#include "btBulletDynamicsCommon.h"

class PhysicsSys : public System {
public:
    PhysicsSys(entt::registry& reg);
    void preinit() override;
    void init() override;
    void tick(float dt) override;
    ~PhysicsSys();
    const char* name() override {return "Physics";};
    
    btDefaultCollisionConfiguration collisionConfiguration;
    btCollisionDispatcher dispatcher;
    btDbvtBroadphase overlappingPairCache;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world;

};

#endif
