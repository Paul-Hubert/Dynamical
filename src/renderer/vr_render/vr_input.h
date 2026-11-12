#ifndef DY_VR_INPUT_H
#define DY_VR_INPUT_H

#include "entt/entt.hpp"

#include "renderer/util/xr.h"

namespace dy {

class Context;

// OpenXR actions : controller poses, trigger, grip, thumbstick and primary button. Results go in the VRInput context
// variable.
class VRActions {
public:
    VRActions(Context& ctx, entt::registry& reg);
    ~VRActions();

    // Called after xrWaitFrame, with the predicted display time of the frame
    void sync(XrTime time);

private:
    void suggest(const char* profile, const std::vector<std::pair<XrAction, const char*>>& bindings);

    Context& ctx;
    entt::registry& reg;

    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction poseAction = XR_NULL_HANDLE;
    XrAction triggerAction = XR_NULL_HANDLE;
    XrAction gripAction = XR_NULL_HANDLE;
    XrAction thumbstickAction = XR_NULL_HANDLE;
    XrAction primaryAction = XR_NULL_HANDLE;
    XrAction stickClickAction = XR_NULL_HANDLE;
    XrPath handPaths[2] = {};
    XrSpace handSpaces[2] = {};
    XrSpace viewSpace = XR_NULL_HANDLE;
};

}

#endif
