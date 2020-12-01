#include "vr_input.h"

#include "renderer/context/context.h"
#include "logic/components/inputc.h"
#include "logic/components/vrinputc.h"
#include "util/util.h"

#include "renderer/context/vr_context.h"

VRInput::VRInput(entt::registry& reg) : reg(reg) {

	VRInputC& vr_input = reg.set<VRInputC>();

	Context& ctx = *reg.ctx<Context*>();

	XrActionSetCreateInfo actionset_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
	strcpy_s(actionset_info.actionSetName, "gameplay");
	strcpy_s(actionset_info.localizedActionSetName, "Gameplay");
	xrCreateActionSet(ctx.vr.instance, &actionset_info, &actionSet);


	xrStringToPath(ctx.vr.instance, "/user/hand/left", &handSubactionPath[0]);
	xrStringToPath(ctx.vr.instance, "/user/hand/right", &handSubactionPath[1]);

	XrActionCreateInfo action_info = { XR_TYPE_ACTION_CREATE_INFO };
	action_info.countSubactionPaths = 2;
	action_info.subactionPaths = handSubactionPath;
	action_info.actionType = XR_ACTION_TYPE_POSE_INPUT;
	strcpy_s(action_info.actionName, "hand_pose");
	strcpy_s(action_info.localizedActionName, "Hand Pose");
	xrCreateAction(actionSet, &action_info, &poseAction);

	// Create an action for listening to the select action! This is primary trigger
	// on controllers, and an airtap on HoloLens
	action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy_s(action_info.actionName, "select");
	strcpy_s(action_info.localizedActionName, "Select");
	xrCreateAction(actionSet, &action_info, &selectAction);

	// Bind the actions we just created to specific locations on the Khronos simple_controller
	// definition! These are labeled as 'suggested' because they may be overridden by the runtime
	// preferences. For example, if the runtime allows you to remap buttons, or provides input
	// accessibility settings.
	XrPath profile_path;
	XrPath pose_path[2];
	XrPath select_path[2];
	xrStringToPath(ctx.vr.instance, "/user/hand/left/input/grip/pose", &pose_path[0]);
	xrStringToPath(ctx.vr.instance, "/user/hand/right/input/grip/pose", &pose_path[1]);
	xrStringToPath(ctx.vr.instance, "/user/hand/left/input/select/click", &select_path[0]);
	xrStringToPath(ctx.vr.instance, "/user/hand/right/input/select/click", &select_path[1]);
	xrStringToPath(ctx.vr.instance, "/interaction_profiles/khr/simple_controller", &profile_path);
	std::vector<XrActionSuggestedBinding> bindings {
		{ poseAction,   pose_path[0]   },
		{ poseAction,   pose_path[1]   },
		{ selectAction, select_path[0] },
		{ selectAction, select_path[1] }, };
	XrInteractionProfileSuggestedBinding suggested_binds = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	suggested_binds.interactionProfile = profile_path;
	suggested_binds.countSuggestedBindings = (uint32_t) bindings.size();
	suggested_binds.suggestedBindings = bindings.data();
	xrSuggestInteractionProfileBindings(ctx.vr.instance, &suggested_binds);

	// Create frames of reference for the pose actions
	for (int32_t i = 0; i < 2; i++) {
		XrActionSpaceCreateInfo action_space_info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
		action_space_info.action = poseAction;
		action_space_info.poseInActionSpace = pose_identity;
		action_space_info.subactionPath = handSubactionPath[i];
		xrCreateActionSpace(ctx.vr.session, &action_space_info, &handSpace[i]);
	}

	// Attach the action set we just made to the session
	XrSessionActionSetsAttachInfo attach_info = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attach_info.countActionSets = 1;
	attach_info.actionSets = &actionSet;
	xrAttachSessionActionSets(ctx.vr.session, &attach_info);

}

void VRInput::poll() {

    OPTICK_EVENT();

    Context& ctx = *reg.ctx<Context*>();
    InputC& input = reg.ctx<InputC>();
	VRInputC& vr_input = reg.ctx<VRInputC>();

    XrEventDataBuffer event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };

    while (xrPollEvent(ctx.vr.instance, &event_buffer) == XR_SUCCESS) {
        switch (event_buffer.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged*)&event_buffer;
                vr_input.session_state = changed->state;

                switch (vr_input.session_state) {
                    case XR_SESSION_STATE_READY: {
                        XrSessionBeginInfo begin_info = {XR_TYPE_SESSION_BEGIN_INFO};
                        begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        xrBeginSession(ctx.vr.session, &begin_info);
						vr_input.rendering = true;
                        break;
                    }
                    case XR_SESSION_STATE_SYNCHRONIZED:
                    case XR_SESSION_STATE_VISIBLE:
                    case XR_SESSION_STATE_FOCUSED:
						vr_input.rendering = true;
                        break;
                    case XR_SESSION_STATE_STOPPING:
                        xrEndSession(ctx.vr.session);
						vr_input.rendering = false;
                        break;
                    case XR_SESSION_STATE_EXITING:
                    case XR_SESSION_STATE_LOSS_PENDING:
                        input.on.set(Action::EXIT);
						vr_input.rendering = false;
                        return;
                    default:
                        break;
                }
            }
            break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                input.on.set(Action::EXIT);
                return;
            default:
                break;
        }
        event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };
    }

	if (vr_input.session_state != XR_SESSION_STATE_FOCUSED)
		return;

	XrActiveActionSet action_set = { };
	action_set.actionSet = actionSet;
	action_set.subactionPath = XR_NULL_PATH;

	XrActionsSyncInfo sync_info = { XR_TYPE_ACTIONS_SYNC_INFO };
	sync_info.countActiveActionSets = 1;
	sync_info.activeActionSets = &action_set;

	xrSyncActions(ctx.vr.session, &sync_info);

	for (uint32_t hand = 0; hand < 2; hand++) {
		XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
		get_info.subactionPath = handSubactionPath[hand];

		XrActionStatePose pose_state = { XR_TYPE_ACTION_STATE_POSE };
		get_info.action = poseAction;
		xrGetActionStatePose(ctx.vr.session, &get_info, &pose_state);
		vr_input.per_hand[hand].active = pose_state.isActive;

		// Events come with a timestamp
		XrActionStateBoolean select_state = { XR_TYPE_ACTION_STATE_BOOLEAN };
		get_info.action = selectAction;
		xrGetActionStateBoolean(ctx.vr.session, &get_info, &select_state);
		vr_input.per_hand[hand].action = select_state.currentState && select_state.changedSinceLastSync;

		if (vr_input.per_hand[hand].action) {
			XrSpaceLocation space_location = { XR_TYPE_SPACE_LOCATION };
			XrResult res = xrLocateSpace(handSpace[hand], ctx.vr.space, select_state.lastChangeTime, &space_location);
			if (XR_UNQUALIFIED_SUCCESS(res) &&
				(space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
				(space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
				vr_input.per_hand[hand].pose = space_location.pose;
			}
		}
	}

}

void VRInput::update() {

	Context& ctx = *reg.ctx<Context*>();
	VRInputC& vr_input = reg.ctx<VRInputC>();

	if (vr_input.session_state != XR_SESSION_STATE_FOCUSED)
		return;
    
	for (size_t hand = 0; hand < 2; hand++) {
		if (!vr_input.per_hand[hand].active) {
			continue;
		}
		XrSpaceLocation spaceRelation = { XR_TYPE_SPACE_LOCATION };
		XrResult res = xrLocateSpace(handSpace[hand], ctx.vr.space, vr_input.predicted_time, &spaceRelation);
		if (XR_UNQUALIFIED_SUCCESS(res) &&
			(spaceRelation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			(spaceRelation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
			vr_input.per_hand[hand].pose = spaceRelation.pose;
		}
	}

}
