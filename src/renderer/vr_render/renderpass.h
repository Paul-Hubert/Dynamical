#ifndef VR_RENDERPASS_H
#define VR_RENDERPASS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <vector>

namespace dy {

class Context;

// Render pass of the headset swapchains, with one framebuffer and depth buffer per eye and swapchain image
class VRRenderpass {
public:
    VRRenderpass(Context& ctx);
    ~VRRenderpass();

    operator vk::RenderPass() { return renderpass; }
    operator VkRenderPass() { return static_cast<VkRenderPass>(renderpass); }

    vk::RenderPass renderpass;

    vk::Format depthFormat;

    struct View {
        std::vector<VmaImage> depthImages;
        std::vector<vk::ImageView> depthViews;
        std::vector<vk::Framebuffer> framebuffers;
    };
    std::vector<View> views;

private:
    Context& ctx;

};

}

#endif
