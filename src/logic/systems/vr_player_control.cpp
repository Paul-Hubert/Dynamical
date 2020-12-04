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
			hand.position_pids.emplace_back(3000, 0, 700, (float) (1./90.), -1000, 1000);
		}

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
				outputs[i] = hand.position_pids[i].tick(inputs[i], targets[i]);
				//dy::log() << outputs[i] << "\n";
			}

			box.rigid_body->applyCentralForce(btVector3(outputs[0], outputs[1], outputs[2]));

		}



		
	});


}

void VRPlayerControlSys::finish() {

}