#ifndef TRANSFORMC_H
#define TRANSFORMC_H

#include <memory>

#include <btBulletDynamicsCommon.h>

#include <glm/gtx/quaternion.hpp>

class TransformC {
public:
	class State : public btMotionState, public btTransform {
	public:
		virtual void getWorldTransform(btTransform& transform) const override {
			transform.setBasis(this->getBasis());
			transform.setOrigin(this->getOrigin());
		}
		virtual void setWorldTransform(const btTransform& transform) override {
			this->setBasis(transform.getBasis());
			this->setOrigin(transform.getOrigin());
		}
	};
	
	TransformC() : transform(std::make_unique<State>()) {}

	std::unique_ptr<State> transform;
	
};

#endif