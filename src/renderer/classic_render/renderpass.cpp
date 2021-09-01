#include "renderpass.h"

#include "renderer/context/context.h"

using namespace dy;

ClRenderpass::ClRenderpass(Context& ctx) : frames(ctx.swap.num_frames), ctx(ctx) {
    
    depthFormat = ctx.swap.findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD16Unorm},
        vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment // | vk::FormatFeatureFlagBits::eTransferSrc
    );

    std::vector<vk::AttachmentDescription> attachments {
            vk::AttachmentDescription({}, vk::Format(ctx.swap.format), vk::SampleCountFlagBits::e1,
                                      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                      vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal
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

    for (int i = 0; i < frames.size(); i++) {
        auto& f = frames[i];

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        f.depthImage = VmaImage(ctx.device, &allocInfo, vk::ImageCreateInfo(
                {}, vk::ImageType::e2D, depthFormat, vk::Extent3D(ctx.swap.extent.width, ctx.swap.extent.height, 1),
                1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment,
                vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined
        ));

        f.depthView = ctx.device->createImageView(
                vk::ImageViewCreateInfo({}, f.depthImage, vk::ImageViewType::e2D, depthFormat,
                                        vk::ComponentMapping(),
                                        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1)));

        auto image_views = std::vector<vk::ImageView>{ctx.swap.imageViews[i], f.depthView};

        f.framebuffer = ctx.device->createFramebuffer(
                vk::FramebufferCreateInfo({}, renderpass, (uint32_t) image_views.size(), image_views.data(), ctx.swap.extent.width, ctx.swap.extent.height, 1));

    }

}


ClRenderpass::~ClRenderpass() {

    for (int i = 0; i < frames.size(); i++) {
        auto& f = frames[i];

        ctx.device->destroy(f.framebuffer);

        ctx.device->destroy(f.depthView);

    }

    ctx.device->destroy(renderpass);
    
}
