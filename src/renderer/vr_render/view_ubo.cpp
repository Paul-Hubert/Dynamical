#include "view_ubo.h"

#include "renderer/context.h"

#include "renderer/num_frames.h"

#include "util/util.h"

ViewUBO::ViewUBO(Context& ctx) : views(ctx.vr.swapchains.size()), ctx(ctx) {

    views.resize(ctx.vr.swapchains.size());

    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, (uint32_t) (NUM_FRAMES*views.size())),
        };
        descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, (uint32_t) (NUM_FRAMES * views.size()), (uint32_t) poolSizes.size(), poolSizes.data()));
    }

    {
        auto bindings = std::vector{
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                               vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        };
        descLayout = ctx.device->createDescriptorSetLayout(
                vk::DescriptorSetLayoutCreateInfo({}, (uint32_t) bindings.size(), bindings.data()));
    }

    for(int i = 0; i<views.size(); i++){
        auto& view = views[i];

        std::vector<vk::DescriptorSetLayout> layouts = Util::nTimes(NUM_FRAMES, descLayout);
        view.descSets = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, NUM_FRAMES, layouts.data()));
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        
        for(int i = 0; i < view.ubos.size(); i++) {

            view.ubos[i] = VmaBuffer(ctx.device, &allocInfo, vk::BufferCreateInfo({}, sizeof(UBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
            
            VmaAllocationInfo inf;
            vmaGetAllocationInfo(ctx.device, view.ubos[i].allocation, &inf);
            view.pointers[i] = static_cast<UBO*> (inf.pMappedData);
            
            auto bufInfo = vk::DescriptorBufferInfo(view.ubos[i], 0, sizeof(UBO));
            
            ctx.device->updateDescriptorSets({
                vk::WriteDescriptorSet(view.descSets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo, nullptr)
            }, {});
            
        }
        
    }
    
}

ViewUBO::~ViewUBO() {
    
    ctx.device->destroy(descLayout);
    
    ctx.device->destroy(descPool);
    
}
