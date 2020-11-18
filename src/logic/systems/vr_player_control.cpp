#include "system_list.h"

#include "logic/components/vrcamerac.h"

#include "util/entt_util.h"

void VRPlayerControlSys::preinit() {

}

void VRPlayerControlSys::init() {

	auto player = reg.ctx<Util::Entity<"player"_hs>>();
	reg.emplace<VRCameraC>(player);

}

void VRPlayerControlSys::tick() {

}

void VRPlayerControlSys::finish() {

}