#include "system_list.h"

#include "util/entt_util.h"

#include <logic/components/playerc.h>

void VRPlayerControlSys::preinit() {

}

void VRPlayerControlSys::init() {

	entt::entity player_entity = reg.ctx<Util::Entity<"player"_hs>>();
	PlayerC& player = reg.get<PlayerC>(player_entity);

	player.left_hand = reg.create();
	player.right_hand = reg.create();
	player.hands[0] = player.left_hand;
	player.hands[1] = player.left_hand;

	for (int h = 0; h < 2; h++) {
		auto hand_entity = player.hands[h];


	}

}

void VRPlayerControlSys::tick() {

	OPTICK_EVENT();

}

void VRPlayerControlSys::finish() {

}