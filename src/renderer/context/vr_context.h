#ifndef VR_CONTEXT_H
#define VR_CONTEXT_H

#include <entt/entt.hpp>

#include "renderer/util/vk.h"
#include "renderer/util/xr.h"

#include <string>
#include <vector>

namespace dy {

class Context;

const XrPosef pose_identity = { {0,0,0,1}, {0,0,0} };

// OpenXR instance, session and swapchains.
// Created before the Vulkan instance because XR decides the Vulkan extensions and physical device.
// If no runtime or headset is available, VR is disabled and the desktop renderer runs alone.
class VRContext {
public:
    VRContext(Context& ctx, entt::registry& reg);
    void init();
    void finish();
    ~VRContext();

    bool enabled() const { return active; }

    std::vector<std::string> vulkanInstanceExtensions();
    std::vector<std::string> vulkanDeviceExtensions();
    uint32_t vulkanApiVersion(uint32_t wanted);
    VkPhysicalDevice vulkanPhysicalDevice(VkInstance instance);

    XrInstance instance = XR_NULL_HANDLE;
#ifndef NDEBUG
    XrDebugUtilsMessengerEXT debug = XR_NULL_HANDLE;
#endif
    XrSystemId system_id = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrViewConfigurationType view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    uint32_t num_views = 0;

    VkFormat swapchain_format = VK_FORMAT_R8G8B8A8_SRGB;
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
    void disable(const char* reason);

    Context& ctx;
    entt::registry& reg;
    bool active = false;
};

}

#endif
