#include "physics.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

#include "logic/components/physicsc.h"

PhysicsSys::PhysicsSys(entt::registry& reg) : System(reg)
/*
collisionConfiguration(),
dispatcher(&collisionConfiguration),
overlappingPairCache(),
solver(),
dynamicsWorld(&dispatcher, &overlappingPairCache, &solver, &collisionConfiguration)
*/
{
    //btDefaultCollisionConfiguration* collisionConfiguration = new btDefaultCollisionConfiguration();

	//dynamicsWorld.setGravity(btVector3(0, -10, 0));

}

void PhysicsSys::preinit() {
    
}

void PhysicsSys::init() {
    
	

}

void PhysicsSys::tick() {



    
    
}

PhysicsSys::~PhysicsSys() {



}