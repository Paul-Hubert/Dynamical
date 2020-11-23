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
    XrDebugUtilsMessengerEXT debug = {};
    XrSystemId system_id;
    XrEnvironmentBlendMode blend;
    XrSession session = {};
    XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
    XrSpace space = {};

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

private:
    Context& ctx;
};

#endif
