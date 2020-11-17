#include "ubo_descriptor.h"

#include "renderer/context.h"

#include "renderer/num_frames.h"

#include "util/util.h"

UBODescriptor::UBODescriptor(Context& ctx) : ctx(ctx) {
    
    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, NUM_FRAMES),
        };
        descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, NUM_FRAMES, poolSizes.size(), poolSizes.data()));
    }
    
    {
        auto bindings = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        };
        descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings.size(), bindings.data()));
        
        std::vector<vk::DescriptorSetLayout> layouts = Util::nTimes(NUM_FRAMES, descLayout);
        descSets = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, NUM_FRAMES, layouts.data()));
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        
        for(int i = 0; i < ubos.size(); i++) {
            
            ubos[i] = VmaBuffer(ctx.device, &allocInfo, vk::BufferCreateInfo({}, sizeof(UBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
            
            VmaAllocationInfo inf;
            vmaGetAllocationInfo(ctx.device, ubos[i].allocation, &inf);
            pointers[i] = static_cast<UBO*> (inf.pMappedData);
            
            auto bufInfo = vk::DescriptorBufferInfo(ubos[i], 0, sizeof(UBO));
            
            ctx.device->updateDescriptorSets({
                vk::WriteDescriptorSet(descSets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo, nullptr)
            }, {});
            
        }
        
    }
    
}

UBODescriptor::~UBODescriptor() {
    
    ctx.device->destroy(descLayout);
    
    ctx.device->destroy(descPool);
    
}
