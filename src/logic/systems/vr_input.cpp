#include "system_list.h"

#include "renderer/context.h"
#include "logic/components/inputc.h"
#include "util/util.h"

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

                switch (ctx.vr.session_state) {
                    case XR_SESSION_STATE_READY: {
                        XrSessionBeginInfo begin_info = {XR_TYPE_SESSION_BEGIN_INFO};
                        begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        xrBeginSession(ctx.vr.session, &begin_info);
                        ctx.vr.rendering = true;
                        break;
                    }
                    case XR_SESSION_STATE_SYNCHRONIZED:
                    case XR_SESSION_STATE_VISIBLE:
                    case XR_SESSION_STATE_FOCUSED:
                        ctx.vr.rendering = true;
                        break;
                    case XR_SESSION_STATE_STOPPING:
                        xrEndSession(ctx.vr.session);
                        ctx.vr.rendering = false;
                        break;
                    case XR_SESSION_STATE_EXITING:
                    case XR_SESSION_STATE_LOSS_PENDING:
                        input.on.set(Action::EXIT);
                        ctx.vr.rendering = false;
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