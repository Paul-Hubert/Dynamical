#ifndef DY_VR_INPUT_H
#define DY_VR_INPUT_H

#include "entt/entt.hpp"

#include "renderer/util/xr.h"

class VRInput {
public:
    VRInput(entt::registry& reg);
    void poll();
    void update();
private:

	XrActionSet actionSet;
	XrAction poseAction;
	XrAction triggerAction;
	XrAction gripAction;
	XrPath handSubactionPath[2];
	XrSpace handSpace[2];

    entt::registry& reg;
};

#endif