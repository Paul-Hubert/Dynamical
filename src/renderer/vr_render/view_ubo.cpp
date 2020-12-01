#include "view_ubo.h"

#include "renderer/context/context.h"

#include "renderer/context/num_frames.h"

#include "util/util.h"

ViewUBO::ViewUBO(Context& ctx) : views(ctx.vr.swapchains.size()), ctx(ctx) {

    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, (uint32_t) (ctx.vr.num_frames * views.size())),
        };
        descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, (uint32_t) (ctx.vr.num_frames * views.size()), (uint32_t) poolSizes.size(), poolSizes.data()));
    }

    {
        auto bindings = std::vector{
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                               vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        };
        descLayout = ctx.device->createDescriptorSetLayout(
                vk::DescriptorSetLayoutCreateInfo({}, (uint32_t) bindings.size(), bindings.data()));
    }

    for(int v = 0; v<views.size(); v++){
        auto& view = views[v];

        view.per_frame.resize(ctx.vr.swapchains[v].num_frames);
        for (int i = 0; i < view.per_frame.size(); i++) {
            auto& f = view.per_frame[i];

            f.set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];
        
            VmaAllocationCreateInfo allocInfo = {};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            f.ubo = VmaBuffer(ctx.device, &allocInfo, vk::BufferCreateInfo({}, sizeof(UBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
            
            VmaAllocationInfo inf;
            vmaGetAllocationInfo(ctx.device, f.ubo.allocation, &inf);
            f.pointer = static_cast<UBO*> (inf.pMappedData);
            
            auto bufInfo = vk::DescriptorBufferInfo(f.ubo, 0, sizeof(UBO));
            
            ctx.device->updateDescriptorSets({
                vk::WriteDescriptorSet(f.set, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo, nullptr)
            }, {});
            
        }
        
    }
    
}

ViewUBO::~ViewUBO() {
    
    ctx.device->destroy(descLayout);
    
    ctx.device->destroy(descPool);
    
}
