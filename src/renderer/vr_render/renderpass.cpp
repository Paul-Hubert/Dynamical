#include "renderpass.h"

#include "renderer/context.h"

Renderpass::Renderpass(Context& ctx) : views(ctx.vr.swapchains.size()), ctx(ctx) {
    
    depthFormat = ctx.swap.findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD16Unorm},
        vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment // | vk::FormatFeatureFlagBits::eTransferSrc
    );

    std::vector<vk::AttachmentDescription> attachments {
            vk::AttachmentDescription({}, vk::Format(ctx.vr.swapchain_format), vk::SampleCountFlagBits::e1,
                                      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                      vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal
            ),
            vk::AttachmentDescription({}, depthFormat, vk::SampleCountFlagBits::e1,
                                      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                                      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                      vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal
            )
    };

    vk::AttachmentReference colorRef(0, vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference depthRef(1, vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal);

    std::vector<vk::SubpassDescription> subpasses {
            vk::SubpassDescription({}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &colorRef, nullptr,
                                   &depthRef, 0, nullptr)
    };

    std::vector<vk::SubpassDependency> dependencies {

    };

    renderpass = ctx.device->createRenderPass( vk::RenderPassCreateInfo({}, (uint32_t) attachments.size(), attachments.data(), (uint32_t) subpasses.size(), subpasses.data(), 0, nullptr));

    for(int v = 0; v<views.size(); v++) {
        auto& swapchain = ctx.vr.swapchains[v];
        auto& view = views[v];

        view.framebuffers.resize(swapchain.images.size());
        view.depthImages.resize(swapchain.images.size());
        view.depthViews.resize(swapchain.images.size());

        for (int i = 0; i < view.framebuffers.size(); i++) {

            VmaAllocationCreateInfo allocInfo = {};
            allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

            view.depthImages[i] = VmaImage(ctx.device, &allocInfo, vk::ImageCreateInfo(
                    {}, vk::ImageType::e2D, depthFormat, vk::Extent3D(swapchain.width, swapchain.height, 1),
                    1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eDepthStencilAttachment,
                    vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined
            ));

            view.depthViews[i] = ctx.device->createImageView(
                    vk::ImageViewCreateInfo({}, view.depthImages[i].image, vk::ImageViewType::e2D, depthFormat,
                                            vk::ComponentMapping(),
                                            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1)));

            auto image_views = std::vector<vk::ImageView>{swapchain.images[i].view, view.depthViews[i]};

            view.framebuffers[i] = ctx.device->createFramebuffer(
                    vk::FramebufferCreateInfo({}, renderpass, (uint32_t) image_views.size(), image_views.data(), swapchain.width, swapchain.height, 1));

        }
    }

}


Renderpass::~Renderpass() {

    for(int v = 0; v<views.size(); v++) {
        auto& view = views[v];

        for (int i = 0; i < view.framebuffers.size(); i++) {

            ctx.device->destroy(view.framebuffers[i]);

            ctx.device->destroy(view.depthViews[i]);

        }

    }

    ctx.device->destroy(renderpass);
    
}
