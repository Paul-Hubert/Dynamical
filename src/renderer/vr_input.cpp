#include "vr_input.h"

#include "renderer/context/context.h"
#include "logic/components/inputc.h"
#include "logic/components/vrinputc.h"
#include "util/util.h"
#include "util/log.h"

#include <string>

#include "renderer/util/vk_util.h"

#include "renderer/context/vr_context.h"

VRInput::VRInput(entt::registry& reg) : reg(reg) {

	VRInputC& vr_input = reg.set<VRInputC>();

	Context& ctx = *reg.ctx<Context*>();

	XrActionSetCreateInfo actionset_info { XR_TYPE_ACTION_SET_CREATE_INFO };
	strcpy(actionset_info.actionSetName, "gameplay");
	strcpy(actionset_info.localizedActionSetName, "Gameplay");
	xrCheckResult(xrCreateActionSet(ctx.vr.instance, &actionset_info, &actionSet));

	xrCheckResult(xrStringToPath(ctx.vr.instance, "/user/hand/left", &handSubactionPath[0]));
	xrCheckResult(xrStringToPath(ctx.vr.instance, "/user/hand/right", &handSubactionPath[1]));

	XrActionCreateInfo action_info { XR_TYPE_ACTION_CREATE_INFO };
	action_info.countSubactionPaths = 2;
	action_info.subactionPaths = handSubactionPath;
	action_info.actionType = XR_ACTION_TYPE_POSE_INPUT;
	strcpy(action_info.actionName, "hand_pose");
	strcpy(action_info.localizedActionName, "Hand Pose");
	xrCheckResult(xrCreateAction(actionSet, &action_info, &poseAction));

	action_info.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	strcpy(action_info.actionName, "trigger");
	strcpy(action_info.localizedActionName, "Trigger");
	xrCheckResult(xrCreateAction(actionSet, &action_info, &triggerAction));

	action_info.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	strcpy(action_info.actionName, "grip");
	strcpy(action_info.localizedActionName, "Grip");
	xrCheckResult(xrCreateAction(actionSet, &action_info, &gripAction));

	

	// Oculus touch

	{

		XrPath profile_path;
		XrPath pose_path[2];
		XrPath trigger_path[2];
		XrPath grip_path[2];
		xrCheckResult(xrStringToPath(ctx.vr.instance, "/user/hand/left/input/grip/pose", &pose_path[0]));
		xrCheckResult(xrStringToPath(ctx.vr.instance, "/user/hand/right/input/grip/pose", &pose_path[1]));
		xrCheckResult(xrStringToPath(ctx.vr.instance, "/user/hand/left/input/trigger/value", &trigger_path[0]));
		xrCheckResult(xrStringToPath(ctx.vr.instance, "/user/hand/right/input/trigger/value", &trigger_path[1]));
		xrCheckResult(xrStringToPath(ctx.vr.instance, "/user/hand/left/input/squeeze/value", &grip_path[0]));
		xrCheckResult(xrStringToPath(ctx.vr.instance, "/user/hand/right/input/squeeze/value", &grip_path[1]));
		xrCheckResult(xrStringToPath(ctx.vr.instance, "/interaction_profiles/oculus/touch_controller", &profile_path));
		std::vector<XrActionSuggestedBinding> bindings{
			{ poseAction, pose_path[0] },
			{ poseAction, pose_path[1] },
			{ triggerAction, trigger_path[0] },
			{ triggerAction, trigger_path[1] },
			{ gripAction, grip_path[0] },
			{ gripAction, grip_path[1] } };
		XrInteractionProfileSuggestedBinding suggested_binds { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		suggested_binds.interactionProfile = profile_path;
		suggested_binds.countSuggestedBindings = (uint32_t) bindings.size();
		suggested_binds.suggestedBindings = bindings.data();
		xrCheckResult(xrSuggestInteractionProfileBindings(ctx.vr.instance, &suggested_binds));

	}

	for (int32_t i = 0; i < 2; i++) {
		XrActionSpaceCreateInfo action_space_info { XR_TYPE_ACTION_SPACE_CREATE_INFO };
		action_space_info.action = poseAction;
		action_space_info.poseInActionSpace = pose_identity;
		action_space_info.subactionPath = handSubactionPath[i];
		xrCheckResult(xrCreateActionSpace(ctx.vr.session, &action_space_info, &handSpace[i]));
	}

	XrSessionActionSetsAttachInfo attach_info { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attach_info.countActionSets = 1;
	attach_info.actionSets = &actionSet;
	xrCheckResult(xrAttachSessionActionSets(ctx.vr.session, &attach_info));

	/*{

		XrPath topLevel;
		xrCheckResult(xrStringToPath(ctx.vr.instance, "/user/hand/left", &topLevel));
		XrInteractionProfileState state {XR_TYPE_INTERACTION_PROFILE_STATE};
		xrCheckResult(xrGetCurrentInteractionProfile(ctx.vr.session, topLevel, &state));
		uint32_t size;
		xrCheckResult(xrPathToString(ctx.vr.instance, state.interactionProfile, 0, &size, nullptr));
		std::vector<char> str(size);
		xrCheckResult(xrPathToString(ctx.vr.instance, state.interactionProfile, size, &size, str.data()));
		dy::log() << str.data() << "\n";

	}*/

}

void VRInput::poll() {

    OPTICK_EVENT();

    Context& ctx = *reg.ctx<Context*>();
    InputC& input = reg.ctx<InputC>();
	VRInputC& vr_input = reg.ctx<VRInputC>();

	vr_input.time = vr_input.predicted_time;

    XrEventDataBuffer event_buffer { XR_TYPE_EVENT_DATA_BUFFER };

    while (xrPollEvent(ctx.vr.instance, &event_buffer) == XR_SUCCESS) {
        switch (event_buffer.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged*)&event_buffer;
                vr_input.session_state = changed->state;

                switch (vr_input.session_state) {
                    case XR_SESSION_STATE_READY: {
                        XrSessionBeginInfo begin_info {XR_TYPE_SESSION_BEGIN_INFO};
                        begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
						xrCheckResult(xrBeginSession(ctx.vr.session, &begin_info));
						vr_input.rendering = true;
                        break;
                    }
                    case XR_SESSION_STATE_SYNCHRONIZED:
                    case XR_SESSION_STATE_VISIBLE:
                    case XR_SESSION_STATE_FOCUSED:
						vr_input.rendering = true;
                        break;
                    case XR_SESSION_STATE_STOPPING:
						xrCheckResult(xrEndSession(ctx.vr.session));
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



	XrActiveActionSet action_set { };
	action_set.actionSet = actionSet;
	action_set.subactionPath = XR_NULL_PATH;

	XrActionsSyncInfo sync_info { XR_TYPE_ACTIONS_SYNC_INFO };
	sync_info.countActiveActionSets = 1;
	sync_info.activeActionSets = &action_set;

	xrCheckResult(xrSyncActions(ctx.vr.session, &sync_info));

	for (uint32_t hand = 0; hand < 2; hand++) {
		XrActionStateGetInfo get_info { XR_TYPE_ACTION_STATE_GET_INFO };
		get_info.subactionPath = handSubactionPath[hand];

		{
			XrActionStatePose pose_state { XR_TYPE_ACTION_STATE_POSE };
			get_info.action = poseAction;
			xrCheckResult(xrGetActionStatePose(ctx.vr.session, &get_info, &pose_state));
			vr_input.hands[hand].active = pose_state.isActive;
		}

		{
			XrActionStateFloat trigger_state {XR_TYPE_ACTION_STATE_FLOAT};
			get_info.action = triggerAction;
			xrCheckResult(xrGetActionStateFloat(ctx.vr.session, &get_info, &trigger_state));
			vr_input.hands[hand].trigger = trigger_state.currentState;
		}

		{
			XrActionStateFloat grip_state{ XR_TYPE_ACTION_STATE_FLOAT };
			get_info.action = gripAction;
			xrCheckResult(xrGetActionStateFloat(ctx.vr.session, &get_info, &grip_state));
			vr_input.hands[hand].grip = grip_state.currentState;
		}
		

		if (!vr_input.hands[hand].active) {
			continue;
		}
		XrSpaceLocation space_location = { XR_TYPE_SPACE_LOCATION };
		XrSpaceVelocity space_velocity = { XR_TYPE_SPACE_VELOCITY };
		space_location.next = &space_velocity;
		XrResult res = xrLocateSpace(handSpace[hand], ctx.vr.space, vr_input.time, &space_location);
		if (XR_UNQUALIFIED_SUCCESS(res) &&
			(space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			(space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0 &&
			(space_velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0 &&
			(space_velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0) {
			vr_input.hands[hand].position = glm::vec3(space_location.pose.position.x, space_location.pose.position.y, space_location.pose.position.z);
			vr_input.hands[hand].rotation = glm::quat(space_location.pose.orientation.x, space_location.pose.orientation.y, space_location.pose.orientation.z, space_location.pose.orientation.w);
			vr_input.hands[hand].linearVelocity = glm::vec3(space_velocity.linearVelocity.x, space_velocity.linearVelocity.y, space_velocity.linearVelocity.z);
			vr_input.hands[hand].angularVelocity = glm::vec3(space_velocity.angularVelocity.x, space_velocity.angularVelocity.y, space_velocity.angularVelocity.z);
		} else {
			vr_input.hands[hand].active = false;
		}
		

	}

}

void VRInput::update() {

	Context& ctx = *reg.ctx<Context*>();
	VRInputC& vr_input = reg.ctx<VRInputC>();

	if (vr_input.session_state != XR_SESSION_STATE_FOCUSED)
		return;
    
	for (size_t hand = 0; hand < 2; hand++) {
		if (!vr_input.hands[hand].active) {
			continue;
		}
		XrSpaceLocation space_location = { XR_TYPE_SPACE_LOCATION };
		XrSpaceVelocity space_velocity = { XR_TYPE_SPACE_VELOCITY };
		space_location.next = &space_velocity;
		XrResult res = xrLocateSpace(handSpace[hand], ctx.vr.space, vr_input.predicted_time, &space_location);
		if (XR_UNQUALIFIED_SUCCESS(res) &&
			(space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			(space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0 &&
			(space_velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0 &&
			(space_velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0) {
			vr_input.hands[hand].predictedPosition = glm::vec3(space_location.pose.position.x, space_location.pose.position.y, space_location.pose.position.z);
			vr_input.hands[hand].predictedRotation = glm::quat(space_location.pose.orientation.x, space_location.pose.orientation.y, space_location.pose.orientation.z, space_location.pose.orientation.w);
			vr_input.hands[hand].predictedLinearVelocity = glm::vec3(space_velocity.linearVelocity.x, space_velocity.linearVelocity.y, space_velocity.linearVelocity.z);
			vr_input.hands[hand].predictedAngularVelocity = glm::vec3(space_velocity.angularVelocity.x, space_velocity.angularVelocity.y, space_velocity.angularVelocity.z);
		} else {
			vr_input.hands[hand].active = false;
		}
	}

}
