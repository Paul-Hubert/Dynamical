#include "transfer.h"

#include "context.h"

#include "util/util.h"

#include <algorithm>
#include <limits>
#include <vector>

using namespace dy;

Transfer::Transfer(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {
    
    pool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient, ctx.device.t_i));

    current.reset(ctx, pool);

    current.command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    
}

void Transfer::Upload::reset(Context& ctx, vk::CommandPool pool) {
    fence = ctx.device->createFence({});
    command = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(pool, vk::CommandBufferLevel::ePrimary, 1))[0];
}

bool Transfer::flush(std::vector<vk::Semaphore> waits, std::vector<vk::PipelineStageFlags> stages, std::vector<vk::Semaphore> signals) {

    OPTICK_EVENT();

    check();
    
    if(!empty) {
        
        current.command.end();

        ctx.device.transfer.submit(vk::SubmitInfo(waits.size(), waits.data(), stages.data(), 1, &current.command, signals.size(), signals.data()), current.fence);

        uploads.push_back(std::move(current));

        //ctx.device->waitForFences({current.fence}, VK_TRUE, std::numeric_limits<uint64_t>::max());

        current.reset(ctx, pool);
        
        current.command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        
        empty = true;
        
        return true;
    }
    return false;
    
}

void Transfer::check() {
    
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
    
}


std::shared_ptr<VmaImage> Transfer::createImage(const void* data, size_t real_size, vk::ImageCreateInfo info, vk::ImageLayout layout) {
    
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

    std::shared_ptr<VmaImage> image = std::make_shared<VmaImage>(ctx.device, &alloc_info, info);

    
    // Data is tightly packed : mip after mip, each one layer after layer (2D arrays) or slice after slice (3D images)
    auto range = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, info.mipLevels, 0, info.arrayLayers);

    auto mipExtent = [&](uint32_t level) {
        return vk::Extent3D(std::max(info.extent.width >> level, 1u), std::max(info.extent.height >> level, 1u), std::max(info.extent.depth >> level, 1u));
    };
    size_t texels = 0;
    for(uint32_t level = 0; level < info.mipLevels; level++) {
        auto extent = mipExtent(level);
        texels += size_t(extent.width) * extent.height * extent.depth * info.arrayLayers;
    }
    const size_t texel_size = real_size / texels;

    std::vector<vk::BufferImageCopy> regions;
    size_t offset = 0;
    for(uint32_t level = 0; level < info.mipLevels; level++) {
        auto extent = mipExtent(level);
        regions.push_back(vk::BufferImageCopy(offset, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, info.arrayLayers), vk::Offset3D(0, 0, 0), extent));
        offset += size_t(extent.width) * extent.height * extent.depth * info.arrayLayers * texel_size;
    }

    current.command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
        {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->image, range
    ));
    
    current.command.copyBufferToImage(stage, image->image, vk::ImageLayout::eTransferDstOptimal, regions);
    
    current.command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, layout,
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->image, range
    ));

    current.uploaded_images.push_back(image);

    empty = false;

    return image;

}


std::shared_ptr<VmaImage> Transfer::createImageMips(const void* data, const std::vector<size_t>& level_sizes, vk::ImageCreateInfo info, vk::ImageLayout layout) {

    size_t total = 0;
    for(size_t size : level_sizes) total += size;

    VmaAllocationCreateInfo ainfo{};
    ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    ainfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    auto binfo = vk::BufferCreateInfo({}, total, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 1, &ctx.device.t_i);

    current.stagingBuffers.push_back(VmaBuffer(ctx.device, &ainfo, binfo));
    auto& stage = current.stagingBuffers.back();

    VmaAllocationInfo inf;
    vmaGetAllocationInfo(ctx.device, stage.allocation, &inf);

    memcpy(inf.pMappedData, data, total);

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    info.mipLevels = (uint32_t) level_sizes.size();
    std::shared_ptr<VmaImage> image = std::make_shared<VmaImage>(ctx.device, &alloc_info, info);

    auto range = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, info.mipLevels, 0, info.arrayLayers);

    current.command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
        {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->image, range
    ));

    std::vector<vk::BufferImageCopy> regions;
    vk::DeviceSize offset = 0;
    for(uint32_t level = 0; level < info.mipLevels; level++) {
        vk::Extent3D extent(std::max(info.extent.width >> level, 1u), std::max(info.extent.height >> level, 1u), 1);
        regions.push_back(vk::BufferImageCopy(offset, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, info.arrayLayers), vk::Offset3D(0, 0, 0), extent));
        offset += level_sizes[level];
    }
    current.command.copyBufferToImage(stage, image->image, vk::ImageLayout::eTransferDstOptimal, regions);

    current.command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
        vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, layout,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->image, range
    ));

    current.uploaded_images.push_back(image);

    empty = false;

    return image;

}


std::shared_ptr<VmaBuffer> Transfer::createBuffer(const void* data, vk::BufferCreateInfo info) {
    
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
        std::shared_ptr<VmaBuffer> buffer = std::make_shared<VmaBuffer>(ctx.device, &alloc_info, info);

        current.command.copyBuffer(stage, buffer->buffer, {vk::BufferCopy(0, 0, buffer->size)});

        current.uploaded_buffers.push_back(buffer);

        empty = false;

        return buffer;

    } else {

        VmaAllocationCreateInfo ainfo{};
        ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ainfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        auto binfo = vk::BufferCreateInfo({}, info.size, vk::BufferUsageFlagBits::eTransferSrc|info.usage, vk::SharingMode::eExclusive, 1, &ctx.device.t_i);
        
        auto buffer = VmaBuffer(ctx.device, &ainfo, binfo);

        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, buffer.allocation, &inf);

        memcpy(inf.pMappedData, data, info.size);

        auto b = std::make_shared<VmaBuffer>(std::move(buffer));
        b->ready = true;
        return b;
        
    }

}

void Transfer::updateImage(const void* data, size_t size, vk::Image image, vk::ImageSubresourceLayers layers, vk::Offset3D offset, vk::Extent3D extent, vk::ImageLayout layout) {

    VmaAllocationCreateInfo ainfo{};
    ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    ainfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    auto binfo = vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 1, &ctx.device.t_i);

    current.stagingBuffers.push_back(VmaBuffer(ctx.device, &ainfo, binfo));
    auto& stage = current.stagingBuffers.back();

    VmaAllocationInfo inf;
    vmaGetAllocationInfo(ctx.device, stage.allocation, &inf);

    memcpy(inf.pMappedData, data, size);

    auto range = vk::ImageSubresourceRange(layers.aspectMask, layers.mipLevel, 1, layers.baseArrayLayer, layers.layerCount);

    current.command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
        vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite, layout, vk::ImageLayout::eTransferDstOptimal,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, range
    ));

    current.command.copyBufferToImage(stage, image, vk::ImageLayout::eTransferDstOptimal, vk::BufferImageCopy(0, 0, 0, layers, offset, extent));

    current.command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
        vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, layout,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, range
    ));

    empty = false;

}

vk::CommandBuffer Transfer::getCommandBuffer() {
    empty = false;
    return current.command;
}

Transfer::~Transfer() {
    
    check();
    
    ctx.device->destroy(current.fence);
    ctx.device->freeCommandBuffers(pool, {current.command});
    
    ctx.device->destroy(pool);
    
}
