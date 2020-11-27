#include "transfer.h"

#include "context.h"

#include "util/util.h"

Transfer::Transfer(Context& ctx) : ctx(ctx) {
    
    pool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient, ctx.device.t_i));

    current.reset(ctx, pool);

    current.command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    
}

void Transfer::Upload::reset(Context& ctx, vk::CommandPool pool) {
    fence = ctx.device->createFence({});
    command = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(pool, vk::CommandBufferLevel::ePrimary, 1))[0];
}

void Transfer::flush() {

    for(auto it = uploads.begin(); it != uploads.end();) {
        Upload& upload = *it;
        vk::Result result = ctx.device->waitForFences({upload.fence}, VK_TRUE, 0);
        if(result == vk::Result::eSuccess) {

            ctx.device->destroy(upload.fence);
            ctx.device->freeCommandBuffers(pool, {upload.command});

            for(auto& buffer : upload.uploaded_buffers) {
                buffer->ready = true;
            }

            for(auto& image : upload.uploaded_images) {
                image->ready = true;
            }

            it = uploads.erase(it);

        } else {
            ++it;
        }
    }
    
    if(!empty) {
        
        current.command.end();
        
        ctx.device.transfer.submit(vk::SubmitInfo(0, nullptr, nullptr, 1, &current.command, 0, nullptr), current.fence);
        
        uploads.push_back(std::move(current));
        current.reset(ctx, pool);
        
        current.command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        
        empty = true;
        
    }
    
}

std::shared_ptr<ImageC> Transfer::createImage(const void* data, size_t real_size, vk::ImageCreateInfo info, vk::ImageLayout layout) {
    
    VmaAllocationCreateInfo ainfo{};
    ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    ainfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    auto binfo = vk::BufferCreateInfo({}, real_size, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 1, &ctx.device.t_i);
    
    current.stagingBuffers.push_back(VmaBuffer(ctx.device, &ainfo, binfo));
    auto& stage = current.stagingBuffers.back();

    VmaAllocationInfo inf;
    vmaGetAllocationInfo(ctx.device, stage.allocation, &inf);

    memcpy(inf.pMappedData, data, real_size);

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    std::shared_ptr<ImageC> image = std::make_shared<ImageC>(ctx.device, &alloc_info, info);

    
    current.command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
        {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    ));
    
    current.command.copyBufferToImage(stage, image->image, vk::ImageLayout::eTransferDstOptimal,
        vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0), info.extent));
    
    current.command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
		vk::AccessFlagBits::eTransferWrite, {}, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    ));

    current.uploaded_images.push_back(image);

    empty = false;

    return image;

}


std::shared_ptr<BufferC> Transfer::createBuffer(const void* data, vk::BufferCreateInfo info) {
    
    if(ctx.device.isDedicated()) {

        VmaAllocationCreateInfo ainfo{};
        ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ainfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        auto binfo = vk::BufferCreateInfo({}, info.size, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 1, &ctx.device.t_i);

        current.stagingBuffers.push_back(VmaBuffer(ctx.device, &ainfo, binfo));
        auto& stage = current.stagingBuffers.back();

        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, stage.allocation, &inf);

        memcpy(inf.pMappedData, data, info.size);

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        info.usage |= vk::BufferUsageFlagBits::eTransferDst;
        std::shared_ptr<BufferC> buffer = std::make_shared<BufferC>(ctx.device, &alloc_info, info);

        current.command.copyBuffer(stage, buffer->buffer, {vk::BufferCopy(0, 0, buffer->buffer.size)});

        current.uploaded_buffers.push_back(buffer);

        empty = false;

        return buffer;

    } else {

        VmaAllocationCreateInfo ainfo{};
        ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ainfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        auto binfo = vk::BufferCreateInfo({}, info.size, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 1, &ctx.device.t_i);
        
        auto buffer = VmaBuffer(ctx.device, &ainfo, binfo);

        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, buffer.allocation, &inf);

        memcpy(inf.pMappedData, data, info.size);

        auto b = std::make_shared<BufferC>(std::move(buffer));
        b->ready = true;
        return b;
        
    }

}

vk::CommandBuffer Transfer::getCommandBuffer() {
    empty = false;
    return current.command;
}

Transfer::~Transfer() {
  
    ctx.device->destroy(pool);
    
}
