#include "system_list.h"

#include "renderer/context.h"
#include "logic/components/inputc.h"

void VRInputSys::preinit() {

}

void VRInputSys::init() {

}

void VRInputSys::tick() {

    Context& ctx = *reg.ctx<Context*>();
    InputC& input = reg.ctx<InputC>();

    XrEventDataBuffer event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };

    while (xrPollEvent(ctx.vr.instance, &event_buffer) == XR_SUCCESS) {
        switch (event_buffer.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged*)&event_buffer;
                ctx.vr.session_state = changed->state;

                // Session state change is where we can begin and end sessions, as well as find quit messages!
                switch (ctx.vr.session_state) {
                    case XR_SESSION_STATE_READY: {
                        XrSessionBeginInfo begin_info = { XR_TYPE_SESSION_BEGIN_INFO };
                        begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        xrBeginSession(ctx.vr.session, &begin_info);
                        input.vr_showing = true;
                    } break;
                    case XR_SESSION_STATE_STOPPING: {
                        input.vr_showing = false;
                        xrEndSession(ctx.vr.session);
                    } break;
                    case XR_SESSION_STATE_EXITING:
                    case XR_SESSION_STATE_LOSS_PENDING:
                        input.on.set(Action::EXIT);
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

}

void VRInputSys::finish() {

}