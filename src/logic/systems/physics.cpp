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

    reg.set<btDiscreteDynamicsWorld*>(&world);

    btGImpactCollisionAlgorithm::registerAlgorithm(&dispatcher);

	world.setGravity(btVector3(0, -1, 0));

	{
		btCollisionShape* groundShape = new btBoxShape(btVector3(btScalar(50.), btScalar(1.), btScalar(50.)));

		btTransform groundTransform;
		groundTransform.setIdentity();
		groundTransform.setOrigin(btVector3(0, -1.0, 0));

		btScalar mass(0.);

		btDefaultMotionState* myMotionState = new btDefaultMotionState(groundTransform);
		btRigidBody* body = new btRigidBody(mass, myMotionState, groundShape);

		//add the body to the dynamics world
		world.addRigidBody(body);
	}

}

void PhysicsSys::preinit() {
    
}

void PhysicsSys::init() {
    
	

}

void PhysicsSys::tick() {

    OPTICK_EVENT();

    world.stepSimulation(1.f / 90.f, 1, 1.f/90.f);
    
}

PhysicsSys::~PhysicsSys() {



}