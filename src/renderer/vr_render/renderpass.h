#ifndef RENDERPASS_H
#define RENDERPASS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

class Context;

class Renderpass {
public:
    Renderpass(Context& ctx);
    ~Renderpass();

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

#endif
