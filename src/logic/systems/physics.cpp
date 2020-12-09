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

	world.setGravity(btVector3(0, -10, 0));

	{
		btCollisionShape* groundShape = new btBoxShape(btVector3(btScalar(50.), btScalar(1.), btScalar(50.)));

		btTransform groundTransform;
		groundTransform.setIdentity();
		groundTransform.setOrigin(btVector3(0, 0, 0));

		btScalar mass(0.);

		btDefaultMotionState* myMotionState = new btDefaultMotionState(groundTransform);
		btRigidBody::btRigidBodyConstructionInfo info(mass, myMotionState, groundShape);
		info.m_friction = 0.5;

		btRigidBody* body = new btRigidBody(info);

		//add the body to the dynamics world
		world.addRigidBody(body);
	}

}

void PhysicsSys::preinit() {
    
}

void PhysicsSys::init() {

}

void PhysicsSys::tick(float dt) {

    OPTICK_EVENT();

    world.stepSimulation(dt, 1, (float)(1. / 90.));
    
}

PhysicsSys::~PhysicsSys() {



}