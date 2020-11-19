#ifndef VR_CONTEXT_H
#define VR_CONTEXT_H

#include "vk.h"

#define XR_USE_GRAPHICS_API_VULKAN
#if defined(WIN32)
#define XR_USE_PLATFORM_WIN32
#include <Windows.h>
#else
#define XR_USE_PLATFORM_XLIB
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

class Context;

class VRContext {
public:
    VRContext(Context& ctx);
    ~VRContext();

    XrInstance instance      = {};
    XrSession session       = {};
    XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
    XrSpace space     = {};
    XrSystemId system_id     = XR_NULL_SYSTEM_ID;
private:
    Context& ctx;
};

#endif
