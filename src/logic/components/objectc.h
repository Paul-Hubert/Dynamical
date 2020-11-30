#ifndef OBJECTC_H
#define OBJECTC_H

#include "BulletCollision/Gimpact/btGImpactShape.h"

class ObjectC {
public:

	btCollisionShape* shape;

	std::unique_ptr<btRigidBody> rigid_body;

};

#endif