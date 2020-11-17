#include "transfer.h"

#include "context.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Transfer::Transfer(Context& ctx) : ctx(ctx) {
    
    pool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer, ctx.device.t_i));
    
    vk::CommandBufferAllocateInfo info(pool, vk::CommandBufferLevel::ePrimary, 2);
    ctx.device->allocateCommandBuffers(&info, commandBuffers.data());
    
    fence = ctx.device->createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
    
    commandBuffers[index].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    
}

void Transfer::flush() {
    
    ctx.device->waitForFences(fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
    
    if(!empty) {
        
        commandBuffers[index].end();
        
        ctx.device->resetFences(fence);
        
        ctx.device.transfer.submit(vk::SubmitInfo(0, nullptr, nullptr, 1, &commandBuffers[index], 0, nullptr), fence);
        
        index = (index+1)%2;
        
        stagingBuffers[index].clear();
        
        commandBuffers[index].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        
        empty = true;
        
    }
    
}

void Transfer::prepareImage(std::string str, VmaImage& image, int num_components = 4, int base_mip = 0, int base_array = 0) {
    
    int x, y, channels;
    stbi_uc* data = stbi_load(str.c_str(), &x, &y, &channels, num_components);

    if(data == nullptr) {
        std::cout << "image " << str << " could not be loaded because : " << stbi_failure_reason() << std::endl;
    }
    
	prepareImage(data, sizeof(stbi_uc) * x * y * num_components, image, vk::Extent3D(x, y, 1), base_mip, base_array);
    
    stbi_image_free(data);
    
}

void Transfer::prepareImage(const void* data, size_t size, VmaImage& image, vk::Extent3D sizes, int base_mip = 0, int base_array = 0) {
    
    VmaAllocationCreateInfo info {};
    info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingBuffers[index].push_back(VmaBuffer(ctx.device, &info, vk::BufferCreateInfo(
        {}, image.size, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 1, &ctx.device.t_i
    )));
    const VmaBuffer& stagingBuffer = stagingBuffers[index].back();
    
    VmaAllocationInfo inf;
    vmaGetAllocationInfo(ctx.device, stagingBuffer.allocation, &inf);
    
    memcpy(inf.pMappedData, data, size);
    
    vk::CommandBuffer buffer = getCommandBuffer();
    
    buffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
        {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image.image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, base_mip, 1, base_array, 1)
    ));
    
    buffer.copyBufferToImage(stagingBuffer.buffer, image.image, vk::ImageLayout::eTransferDstOptimal,
        vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, base_mip, base_array, 1), vk::Offset3D(0, 0, 0), vk::Extent3D(sizes.width, sizes.height, sizes.depth)));
    
    buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
		vk::AccessFlagBits::eTransferWrite, {},
        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image.image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, base_mip, 1, base_array, 1)
    ));
    
}


bool Transfer::prepareBuffer(const void* data, VmaBuffer& buffer) {
    
    bool needsStaging = ctx.device.isDedicated();
    
    
    if(needsStaging) {
        
        VmaAllocationCreateInfo info {};
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        stagingBuffers[index].push_back(VmaBuffer(ctx.device, &info, vk::BufferCreateInfo(
            {}, buffer.size, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 1, &ctx.device.t_i
        )));
        const VmaBuffer& stagingBuffer = stagingBuffers[index].back();
        
        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, stagingBuffer.allocation, &inf);
        
        memcpy(inf.pMappedData, data, buffer.size);
        
        vk::CommandBuffer commandBuffer = getCommandBuffer();
        
        commandBuffer.copyBuffer(stagingBuffer.buffer, buffer.buffer, vk::BufferCopy(0, 0, buffer.size));
        
    } else {
        
        void* dest = ctx.device->mapMemory(buffer.memory, buffer.offset, buffer.size, {});
        
        memcpy(dest, data, buffer.size);
        
        ctx.device->unmapMemory(buffer.memory);
        
    }
    
    return !needsStaging;
    
}

vk::CommandBuffer Transfer::getCommandBuffer() {
    empty = false;
    return commandBuffers[index];
}

Transfer::~Transfer() {
    
    for(int i = 0; i<2; i++) {
        stagingBuffers[i].clear();
    }
    
    ctx.device->destroy(fence);
    
    ctx.device->destroy(pool);
    
}
