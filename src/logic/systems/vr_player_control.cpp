#include "system_list.h"

#include "util/entt_util.h"

#include "util/log.h"

#include "logic/components/vrhandc.h"

#include "logic/components/playerc.h"
#include "renderer/model/model_manager.h"
#include "logic/components/model_renderablec.h"
#include "logic/components/transformc.h"
#include "logic/components/objectc.h"
#include "renderer/model/model_manager.h"
#include "logic/components/vrinputc.h"

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

		auto box_model = reg.ctx<ModelManager>().get("./resources/box.glb");

		auto& renderable = reg.emplace<ModelRenderableC>(entity, box_model);
		auto& transform = reg.emplace<TransformC>(entity);
		transform.transform->setIdentity();

		ObjectC& box_object = reg.emplace<ObjectC>(entity);

		auto& model = reg.get<ModelC>(box_model);
		box_object.rigid_body = std::make_unique<btRigidBody>(model.mass, static_cast<btMotionState*>(transform.transform.get()), model.shape.get());
		box_object.rigid_body->setDamping(0.01f, 0.01f);

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
		
		if (!hand.active) {
			hand.active = true;

			transform.transform->setOrigin(btVector3(vr_input.hands[hand.index].position.x, vr_input.hands[hand.index].position.y, vr_input.hands[hand.index].position.z));
			transform.transform->setRotation(btQuaternion(vr_input.hands[hand.index].rotation.x, vr_input.hands[hand.index].rotation.y, vr_input.hands[hand.index].rotation.z, vr_input.hands[hand.index].rotation.w));

			hand.last_predicted_linear_velocity = vr_input.hands[hand.index].linearVelocity;
			hand.last_predicted_angular_velocity = vr_input.hands[hand.index].angularVelocity;
			hand.last_dt = dt;

			reg.ctx<btDiscreteDynamicsWorld*>()->addRigidBody(box.rigid_body.get());
			return;

		}
		

		box.rigid_body->clearForces();
		{

			glm::vec3 current_position = glm::vec3(transform.transform->getOrigin().x(), transform.transform->getOrigin().y(), transform.transform->getOrigin().z());
			glm::vec3 current_velocity = glm::vec3(box.rigid_body->getLinearVelocity().x(), box.rigid_body->getLinearVelocity().y(), box.rigid_body->getLinearVelocity().z());

			hand.last_load = box.rigid_body->getMass() * (hand.last_predicted_linear_velocity - current_velocity) / hand.last_dt;
			glm::vec3 force = 2.f * box.rigid_body->getMass() * ((vr_input.hands[hand.index].position - current_position) / dt - current_velocity) / dt - hand.last_load;

			float f = glm::length(force);
			const float max_force = 1000.f;
			if (f > max_force) {
				force = force * max_force / f;
			}

			hand.last_predicted_linear_velocity = force * box.rigid_body->getInvMass() * dt + current_velocity;

			//dy::log() << "load : " << hand.last_load.x << " " << hand.last_load.y << " " << hand.last_load.z << "\n";
			//dy::log() << "force : " << force.x << " " << force.y << " " << force.z << "\n";

			box.rigid_body->applyCentralForce(btVector3(force.x, force.y, force.z));
		}

		/*{

			glm::vec3 current_rotation = glm::vec3(transform.transform->getOrigin().x(), transform.transform->getOrigin().y(), transform.transform->getOrigin().z());
			glm::vec3 current_angular = glm::vec3(box.rigid_body->getLinearVelocity().x(), box.rigid_body->getLinearVelocity().y(), box.rigid_body->getLinearVelocity().z());

			hand.last_load = (hand.last_predicted_angular_velocity - current_angular) / hand.last_dt * box.rigid_body->getInvInertiaTensorWorld();
			glm::vec3 torque = 2.f * box.rigid_body->getMass() * ((vr_input.hands[hand.index].position - current_position) / dt - current_velocity) / dt - hand.last_load;

			float t = glm::length(torque);
			const float max_torque = 1000.f;
			if (f > max_torque) {
				torque = torque * max_torque / t;
			}

			hand.last_predicted_linear_velocity = torque * box.rigid_body->getInvMass() * dt + current_velocity;

			box.rigid_body->applyTorque(btVector3(torque.x, torque.y, torque.z));
		}*/


		hand.last_dt = dt;

		

		
	});


}

void VRPlayerControlSys::finish() {

}