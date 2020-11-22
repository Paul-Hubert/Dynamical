#ifndef VR_CONTEXT_H
#define VR_CONTEXT_H

#include "vk.h"
#include "vmapp.h"

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

void xrCheckResult(XrResult result);

class VRContext {
public:
    VRContext(Context& ctx);
    void init();
    void finish();
    ~VRContext();

    XrInstance instance = {};
    XrSession session = {};
    XrDebugUtilsMessengerEXT debug = {};
    XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
    XrSpace space = {};
    XrSystemId system_id = XR_NULL_SYSTEM_ID;

    VkFormat swapchain_format = VK_FORMAT_R8G8B8A8_UNORM;
    struct swapchain {
        XrSwapchain handle;
        uint32_t width;
        uint32_t height;
        struct image {
            VkImage image;
            VkImageView view;
        };
        std::vector<image> images;
    };
    std::vector<swapchain> swapchains;

    struct input_state_t {
        XrActionSet actionSet;
        XrAction    poseAction;
        XrAction    selectAction;
        XrPath   handSubactionPath[2];
        XrSpace  handSpace[2];
        XrPosef  handPose[2];
        XrBool32 renderHand[2];
        XrBool32 handSelect[2];
    };
    input_state_t input;

private:
    Context& ctx;
};

#endif
