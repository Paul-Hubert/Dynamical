#include "system_list.h"

#include "util/entt_util.h"

#include "util/log.h"

#include "extra/PID/PID.h"

#include "logic/components/vrhandc.h"

#include "logic/components/playerc.h"
#include "renderer/model/model_manager.h"
#include "logic/components/model_renderablec.h"
#include "logic/components/transformc.h"
#include "logic/components/objectc.h"
#include "renderer/model/model_manager.h"
#include "logic/components/vrinputc.h"

const float max_force = 1000;
const float max_torque = 100;

#ifndef M_PI
#define M_PI    3.14159265358979323846264338327950288   /**< pi */
#endif

void VRPlayerControlSys::preinit() {

}

void VRPlayerControlSys::init() {

	entt::entity player_entity = reg.ctx<Util::Entity<"player"_hs>>();
	PlayerC& player = reg.get<PlayerC>(player_entity);

	player.left_hand = reg.create();
	player.right_hand = reg.create();

	for (int h = 0; h < 2; h++) {

		auto entity = h == VRHandC::left ? player.left_hand : player.right_hand;
		auto& hand = reg.emplace<VRHandC>(entity);
		hand.index = h;
		for (int i = 0; i < 3; i++) {
			hand.position_pids.emplace_back(1700.f, 0.f, 500.f, (float) (1./90.), -max_force, max_force);
		}
		for (int i = 0; i < 3; i++) {
			hand.rotation_pids.emplace_back(0.0f, 0.f, 0.f, (float)(1. / 90.), -max_torque, max_torque);
			hand.rotation_pids[i].setWrapped(0.f, (float) (2.f*M_PI));
		}

		auto box_model = reg.ctx<ModelManager>().get("./resources/box.glb");

		auto& renderable = reg.emplace<ModelRenderableC>(entity, box_model);
		auto& transform = reg.emplace<TransformC>(entity);
		transform.transform->setIdentity();

		ObjectC& box_object = reg.emplace<ObjectC>(entity);

		auto& model = reg.get<ModelC>(box_model);
		btRigidBody::btRigidBodyConstructionInfo info(model.mass, static_cast<btMotionState*>(transform.transform.get()), model.shape.get(), model.local_inertia);
		info.m_friction = 0.5;
		//info.m_linearDamping = 0.01;
		box_object.rigid_body = std::make_unique<btRigidBody>(info);

	}

}

void VRPlayerControlSys::tick(float dt) {

	OPTICK_EVENT();

	VRInputC& vr_input = reg.ctx<VRInputC>();

	entt::entity player_entity = reg.ctx<Util::Entity<"player"_hs>>();
	PlayerC& player = reg.get<PlayerC>(player_entity);


	reg.view<VRHandC, ObjectC, TransformC>().each([&](entt::entity entity, VRHandC& hand, ObjectC& box, TransformC& transform) {

		if (!vr_input.hands[hand.index].active) {
			if (hand.active) {
				hand.active = false;
				reg.ctx<btDiscreteDynamicsWorld*>()->removeRigidBody(box.rigid_body.get());
			}
			return;
		}

		auto& h = vr_input.hands[hand.index];
		
		if (!hand.active) {
			hand.active = true;

			transform.transform->setOrigin(btVector3(h.position.x, h.position.y, h.position.z));
			transform.transform->setRotation(btQuaternion(h.rotation.x, h.rotation.y, h.rotation.z, h.rotation.w));
			box.rigid_body->setWorldTransform(*transform.transform);

			reg.ctx<btDiscreteDynamicsWorld*>()->addRigidBody(box.rigid_body.get());
			box.rigid_body->setLinearVelocity(btVector3(0, 0, 0));
			return;

		}
		
		
		glm::vec3 position(transform.transform->getOrigin().x(), transform.transform->getOrigin().y(), transform.transform->getOrigin().z());
		if (glm::distance(vr_input.hands[hand.index].position, position) > 1) {
			transform.transform->setOrigin(btVector3(h.position.x, h.position.y, h.position.z));
			transform.transform->setRotation(btQuaternion(h.rotation.x, h.rotation.y, h.rotation.z, h.rotation.w));
			box.rigid_body->setWorldTransform(*transform.transform);
			box.rigid_body->setLinearVelocity(btVector3(0,0,0));
		}

		box.rigid_body->clearForces();
		box.rigid_body->activate(true);

		/*{

			glm::vec3 position(transform.transform->getOrigin().x(), transform.transform->getOrigin().y(), transform.transform->getOrigin().z());
			glm::vec3 velocity(box.rigid_body->getLinearVelocity().x(), box.rigid_body->getLinearVelocity().y(), box.rigid_body->getLinearVelocity().z());
			glm::vec3 load = (velocity - hand.last_velocity) * box.rigid_body->getMass() / hand.last_dt - hand.last_force;

			hand.last_velocity = velocity;
			hand.last_force = ((vr_input.hands[hand.index].position - position) / dt - hand.last_velocity) * box.rigid_body->getMass() / dt - load;
			hand.last_dt = dt;

			hand.last_force = glm::clamp(hand.last_force, glm::vec3(-max_force), glm::vec3(max_force));

			glm::vec3 error = vr_input.hands[hand.index].position - position;

			if (vr_input.hands[0].trigger > 0.1) {
				hand.damping += 1;
				dy::log() << "D : " << hand.damping << "\n";
			} if (vr_input.hands[0].grip > 0.1) {
				hand.damping -= 1;
				dy::log() << "D : " << hand.damping << "\n";
			}
			hand.last_force *= 0.50f;// (error - hand.last_error) / dt;
			hand.last_error = error;

			//box.rigid_body->setLinearVelocity(btVector3(hand.last_velocity.x, hand.last_velocity.y, hand.last_velocity.z));

			box.rigid_body->applyCentralForce(btVector3(hand.last_force.x, hand.last_force.y, hand.last_force.z));
		}*/

		// Position
		{

			float targets[3];
			targets[0] = h.predictedPosition.x;
			targets[1] = h.predictedPosition.y;
			targets[2] = h.predictedPosition.z;

			float inputs[3];
			inputs[0] = transform.transform->getOrigin().x();
			inputs[1] = transform.transform->getOrigin().y();
			inputs[2] = transform.transform->getOrigin().z();

			float outputs[3];

			for (int i = 0; i < 3; i++) {
				/*
				if (vr_input.hands[0].trigger > 0.1) {
					hand.position_pids[i].i += 1;
					dy::log() << "I : " << hand.position_pids[i].i << "\n";
				} if (vr_input.hands[0].grip > 0.1) {
					hand.position_pids[i].i -= 1;
					dy::log() << "I : " << hand.position_pids[i].i << "\n";
				} if (vr_input.hands[1].trigger > 0.1) {
					hand.position_pids[i].d += 1;
					dy::log() << "D : " << hand.position_pids[i].d << "\n";
				} if (vr_input.hands[1].grip > 0.1) {
					hand.position_pids[i].d -= 1;
					dy::log() << "D : " << hand.position_pids[i].d << "\n";
				}
				*/
				outputs[i] = hand.position_pids[i].tick(inputs[i], targets[i]);
			}

			box.rigid_body->applyCentralForce(btVector3(outputs[0], outputs[1], outputs[2]));

		}


		// Rotation
		{

			glm::vec3 target = glm::eulerAngles(h.predictedRotation);

			float targets[3];
			targets[0] = target.x;
			targets[1] = target.y;
			targets[2] = target.z;

			glm::vec3 current = glm::eulerAngles(glm::quat(transform.transform->getRotation().x(), transform.transform->getRotation().y(), transform.transform->getRotation().z(), transform.transform->getRotation().w()));

			float inputs[3];
			inputs[0] = current.x;
			inputs[1] = current.y;
			inputs[2] = current.z;

			float outputs[3];

			for (int i = 0; i < 3; i++) {
				
				if (vr_input.hands[0].trigger > 0.1) {
					hand.rotation_pids[i].p += 0.001f;
					dy::log() << "P : " << hand.rotation_pids[i].p << "\n";
				} if (vr_input.hands[0].grip > 0.1) {
					hand.rotation_pids[i].p -= 0.001f;
					dy::log() << "P : " << hand.rotation_pids[i].p << "\n";
				} if (vr_input.hands[1].trigger > 0.1) {
					hand.rotation_pids[i].d += 0.001f;
					dy::log() << "D : " << hand.rotation_pids[i].d << "\n";
				} if (vr_input.hands[1].grip > 0.1) {
					hand.rotation_pids[i].d -= 0.001f;
					dy::log() << "D : " << hand.rotation_pids[i].d << "\n";
				}
				
				outputs[i] = hand.rotation_pids[i].tick(inputs[i], targets[i]);
			}

			box.rigid_body->applyTorque(btVector3(outputs[1], outputs[2], outputs[0]));

		}


		
	});


}

void VRPlayerControlSys::finish() {

}