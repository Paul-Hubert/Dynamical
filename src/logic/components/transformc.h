#ifndef TRANSFORMC_H
#define TRANSFORMC_H

#include "btBulletDynamicsCommon.h"

class TransformC : public btMotionState {
public:

	btTransform transform;

	virtual void getWorldTransform(btTransform& transform) const override {
		transform = this->transform;
	}

	virtual void setWorldTransform(const btTransform& transform) override {
		this->transform = transform;
	}

};

#endif