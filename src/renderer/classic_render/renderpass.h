#ifndef CLASSIC_RENDERPASS_H
#define CLASSIC_RENDERPASS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

class Context;

class ClRenderpass {
public:
    ClRenderpass(Context& ctx);
    ~ClRenderpass();

    operator vk::RenderPass() { return renderpass; }
    operator VkRenderPass() { return static_cast<VkRenderPass>(renderpass); }

    vk::RenderPass renderpass;

    vk::Format depthFormat;

    struct per_frame {
        VmaImage depthImage;
        vk::ImageView depthView;
        vk::Framebuffer framebuffer;
    };
    std::vector<per_frame> frames;

    
private:
    Context& ctx;
    
};

#endif
