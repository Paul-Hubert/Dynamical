#include "renderpass.h"

#include "renderer/device.h"
#include "renderer/swapchain.h"
#include "renderer/num_frames.h"

Renderpass::Renderpass(Device& device, Swapchain& swap) :
depthImages(swap.num_frames), depthViews(swap.num_frames), framebuffers(swap.num_frames),
device(device), swap(swap) {
    
    depthFormat = swap.findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD16Unorm},
        vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment // | vk::FormatFeatureFlagBits::eTransferSrc
    );
    
    
    
    auto attachments = std::vector<vk::AttachmentDescription> {
        vk::AttachmentDescription({}, vk::Format(swap.format), vk::SampleCountFlagBits::e1, 
                                  vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                  vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                  vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR
                                 ),
        vk::AttachmentDescription({}, depthFormat, vk::SampleCountFlagBits::e1, 
                                  vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                                  vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                  vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR
                                 )
    };
    
    auto colorRef = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);
    auto depthRef = vk::AttachmentReference(1, vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal);
    
    auto subpasses = std::vector<vk::SubpassDescription> {
        vk::SubpassDescription({}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &colorRef, nullptr, &depthRef, 0, nullptr)
    };
    
    auto dependencies = std::vector<vk::SubpassDependency> {
        
    };
    
    renderpass = device->createRenderPass(vk::RenderPassCreateInfo({}, attachments.size(), attachments.data(), subpasses.size(), subpasses.data(), 0, nullptr));
    
}

void Renderpass::setup() {
    
    for(int i = 0; i<framebuffers.size(); i++) {
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        
        depthImages[i] = VmaImage(device, &allocInfo, vk::ImageCreateInfo(
            {}, vk::ImageType::e2D, depthFormat, vk::Extent3D(swap.extent.width, swap.extent.height, 1), 
            1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, // | vk::ImageUsageFlagBits::eTransferSrc,
            vk::SharingMode::eExclusive, 1, &device.g_i, vk::ImageLayout::eUndefined
        ));
        
        depthViews[i] = device->createImageView(vk::ImageViewCreateInfo({}, depthImages[i].image, vk::ImageViewType::e2D, depthFormat, vk::ComponentMapping(),
                                                                        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1)));
        
        auto views = std::vector<vk::ImageView> {swap.imageViews[i], depthViews[i]};
        
        framebuffers[i] = device->createFramebuffer(vk::FramebufferCreateInfo({}, renderpass, 2, views.data(), swap.extent.width, swap.extent.height, 1));
        
    }
    
}

void Renderpass::cleanup() {
    
    for(int i = 0; i<framebuffers.size(); i++) {
        
        device->destroy(framebuffers[i]);
        
        device->destroy(depthViews[i]);
        
    }
    
}


Renderpass::~Renderpass() {
    
    device->destroy(renderpass);
    
}
