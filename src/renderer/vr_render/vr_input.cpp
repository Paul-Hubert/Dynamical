#include "vr_input.h"

#include "renderer/context/context.h"
#include "logic/components/vr.h"
#include "util/log.h"

#include <cstring>
#include <vector>

using namespace dy;

static VRPose toPose(const XrSpaceLocation& location) {
    VRPose pose;
    pose.valid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
    pose.position = glm::vec3(location.pose.position.x, location.pose.position.y, location.pose.position.z);
    pose.rotation = glm::quat(location.pose.orientation.w, location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z);
    return pose;
}

VRActions::VRActions(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    auto& input = reg.ctx().emplace<VRInput>();
    input.enabled = ctx.vr.enabled();

    if(!ctx.vr.enabled()) {
        return;
    }

    XrInstance instance = ctx.vr.instance;

    XrActionSetCreateInfo actionset_info { XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy(actionset_info.actionSetName, "gameplay");
    strcpy(actionset_info.localizedActionSetName, "Gameplay");
    xrCheckResult(xrCreateActionSet(instance, &actionset_info, &actionSet));

    xrCheckResult(xrStringToPath(instance, "/user/hand/left", &handPaths[VRInput::left]));
    xrCheckResult(xrStringToPath(instance, "/user/hand/right", &handPaths[VRInput::right]));

    auto makeAction = [&](XrActionType type, const char* name, const char* localized, XrAction& action) {
        XrActionCreateInfo action_info { XR_TYPE_ACTION_CREATE_INFO };
        action_info.countSubactionPaths = 2;
        action_info.subactionPaths = handPaths;
        action_info.actionType = type;
        strcpy(action_info.actionName, name);
        strcpy(action_info.localizedActionName, localized);
        xrCheckResult(xrCreateAction(actionSet, &action_info, &action), name);
    };

    makeAction(XR_ACTION_TYPE_POSE_INPUT, "hand_pose", "Hand Pose", poseAction);
    makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", triggerAction);
    makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Grip", gripAction);
    makeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", thumbstickAction);

    suggest("/interaction_profiles/oculus/touch_controller", {
        {poseAction, "/user/hand/left/input/grip/pose"},
        {poseAction, "/user/hand/right/input/grip/pose"},
        {triggerAction, "/user/hand/left/input/trigger/value"},
        {triggerAction, "/user/hand/right/input/trigger/value"},
        {gripAction, "/user/hand/left/input/squeeze/value"},
        {gripAction, "/user/hand/right/input/squeeze/value"},
        {thumbstickAction, "/user/hand/left/input/thumbstick"},
        {thumbstickAction, "/user/hand/right/input/thumbstick"},
    });

    suggest("/interaction_profiles/valve/index_controller", {
        {poseAction, "/user/hand/left/input/grip/pose"},
        {poseAction, "/user/hand/right/input/grip/pose"},
        {triggerAction, "/user/hand/left/input/trigger/value"},
        {triggerAction, "/user/hand/right/input/trigger/value"},
        {gripAction, "/user/hand/left/input/squeeze/value"},
        {gripAction, "/user/hand/right/input/squeeze/value"},
        {thumbstickAction, "/user/hand/left/input/thumbstick"},
        {thumbstickAction, "/user/hand/right/input/thumbstick"},
    });

    suggest("/interaction_profiles/khr/simple_controller", {
        {poseAction, "/user/hand/left/input/grip/pose"},
        {poseAction, "/user/hand/right/input/grip/pose"},
        {triggerAction, "/user/hand/left/input/select/click"},
        {triggerAction, "/user/hand/right/input/select/click"},
    });

    for(int hand = 0; hand < 2; hand++) {
        XrActionSpaceCreateInfo action_space_info { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        action_space_info.action = poseAction;
        action_space_info.poseInActionSpace = pose_identity;
        action_space_info.subactionPath = handPaths[hand];
        xrCheckResult(xrCreateActionSpace(ctx.vr.session, &action_space_info, &handSpaces[hand]));
    }

    XrReferenceSpaceCreateInfo view_info { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    view_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    view_info.poseInReferenceSpace = pose_identity;
    xrCheckResult(xrCreateReferenceSpace(ctx.vr.session, &view_info, &viewSpace));

    XrSessionActionSetsAttachInfo attach_info { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attach_info.countActionSets = 1;
    attach_info.actionSets = &actionSet;
    xrCheckResult(xrAttachSessionActionSets(ctx.vr.session, &attach_info));

}

void VRActions::suggest(const char* profile, const std::vector<std::pair<XrAction, const char*>>& bindings) {

    XrInstance instance = ctx.vr.instance;

    std::vector<XrActionSuggestedBinding> suggested;
    for(auto& [action, path] : bindings) {
        XrPath xr_path;
        xrCheckResult(xrStringToPath(instance, path, &xr_path), path);
        suggested.push_back({action, xr_path});
    }

    XrPath profile_path;
    xrCheckResult(xrStringToPath(instance, profile, &profile_path), profile);

    XrInteractionProfileSuggestedBinding suggested_binds { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    suggested_binds.interactionProfile = profile_path;
    suggested_binds.countSuggestedBindings = (uint32_t) suggested.size();
    suggested_binds.suggestedBindings = suggested.data();
    // Runtimes that do not know a profile reject it, which is fine
    xrSuggestInteractionProfileBindings(instance, &suggested_binds);

}

void VRActions::sync(XrTime time) {

    if(!ctx.vr.enabled()) {
        return;
    }

    OPTICK_EVENT();

    auto& input = reg.ctx().get<VRInput>();

    {
        XrSpaceLocation location { XR_TYPE_SPACE_LOCATION };
        if(XR_SUCCEEDED(xrLocateSpace(viewSpace, ctx.vr.space, time, &location))) {
            input.head = toPose(location);
        }
    }

    if(!input.focused) {
        for(auto& hand : input.hands) {
            hand = VRInput::Hand();
        }
        return;
    }

    XrActiveActionSet action_set { };
    action_set.actionSet = actionSet;
    action_set.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo sync_info { XR_TYPE_ACTIONS_SYNC_INFO };
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets = &action_set;
    xrCheckResult(xrSyncActions(ctx.vr.session, &sync_info));

    for(int i = 0; i < 2; i++) {
        auto& hand = input.hands[i];

        XrActionStateGetInfo get_info { XR_TYPE_ACTION_STATE_GET_INFO };
        get_info.subactionPath = handPaths[i];

        XrActionStateFloat trigger_state { XR_TYPE_ACTION_STATE_FLOAT };
        get_info.action = triggerAction;
        xrCheckResult(xrGetActionStateFloat(ctx.vr.session, &get_info, &trigger_state));
        bool was_pressed = hand.trigger > 0.8f;
        hand.trigger = trigger_state.isActive ? trigger_state.currentState : 0.f;
        hand.trigger_pressed = !was_pressed && hand.trigger > 0.8f;

        XrActionStateFloat grip_state { XR_TYPE_ACTION_STATE_FLOAT };
        get_info.action = gripAction;
        xrCheckResult(xrGetActionStateFloat(ctx.vr.session, &get_info, &grip_state));
        hand.grip = grip_state.isActive ? grip_state.currentState : 0.f;

        XrActionStateVector2f stick_state { XR_TYPE_ACTION_STATE_VECTOR2F };
        get_info.action = thumbstickAction;
        xrCheckResult(xrGetActionStateVector2f(ctx.vr.session, &get_info, &stick_state));
        hand.thumbstick = stick_state.isActive ? glm::vec2(stick_state.currentState.x, stick_state.currentState.y) : glm::vec2(0.f);

        XrSpaceLocation location { XR_TYPE_SPACE_LOCATION };
        if(XR_SUCCEEDED(xrLocateSpace(handSpaces[i], ctx.vr.space, time, &location))) {
            hand.pose = toPose(location);
        } else {
            hand.pose.valid = false;
        }
    }

}

VRActions::~VRActions() {

    if(viewSpace != XR_NULL_HANDLE) xrDestroySpace(viewSpace);
    for(auto space : handSpaces) {
        if(space != XR_NULL_HANDLE) xrDestroySpace(space);
    }
    if(actionSet != XR_NULL_HANDLE) xrDestroyActionSet(actionSet);

}
