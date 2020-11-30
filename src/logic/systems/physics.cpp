#include "physics.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

#include "logic/components/objectc.h"

#include <BulletCollision/Gimpact/btGImpactCollisionAlgorithm.h>

PhysicsSys::PhysicsSys(entt::registry& reg) : System(reg),
collisionConfiguration(),
dispatcher(&collisionConfiguration),
overlappingPairCache(),
solver(),
world(&dispatcher, &overlappingPairCache, &solver, &collisionConfiguration)
{

    btGImpactCollisionAlgorithm::registerAlgorithm(&dispatcher);

	//world.setGravity(btVector3(0, -10, 0));


}

void PhysicsSys::preinit() {
    
}

void PhysicsSys::init() {
    
	

}

void PhysicsSys::tick() {

    OPTICK_EVENT();

    world.stepSimulation(0.1f, 1, 1.f/90.f);
    
}

PhysicsSys::~PhysicsSys() {



}