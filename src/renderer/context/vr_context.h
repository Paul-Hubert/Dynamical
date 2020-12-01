#ifndef VR_CONTEXT_H
#define VR_CONTEXT_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include "renderer/util/xr.h"

class Context;

const XrPosef pose_identity = { {0,0,0,1}, {0,0,0} };

class VRContext {
public:
    VRContext(Context& ctx);
    void init();
    void finish();
    ~VRContext();

    XrInstance instance = {};
#ifndef NDEBUG
    XrDebugUtilsMessengerEXT debug = {};
#endif
    XrSystemId system_id;
    XrEnvironmentBlendMode blend;
    XrSession session = {};
    XrSpace space = {};

    uint32_t num_frames = 0;

    VkFormat swapchain_format = VK_FORMAT_R8G8B8A8_SRGB;
    struct swapchain {
        XrSwapchain handle;
        uint32_t width;
        uint32_t height;
        uint32_t num_frames;
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
