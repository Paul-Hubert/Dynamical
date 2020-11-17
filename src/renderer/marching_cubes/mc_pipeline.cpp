#include "mc_pipeline.h"

#include "renderer/device.h"
#include "util/util.h"
#include "terrain.h"

#include "logic/components/chunk/chunkc.h"

#include "glm/glm.hpp"

#include <iostream>

MCPipeline::MCPipeline(Device& device, vk::DescriptorSetLayout setlayout) : device(device) {
    
    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, 1),
    };
    descPool = device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, poolSizes.size(), poolSizes.data()));
    
    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eCompute),
    };
    descLayout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings.size(), bindings.data()));
    
    
    
    descSet = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];
    
    
    auto pcrange = vk::PushConstantRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(MCPushConstants));
    
    auto layouts = std::vector {descLayout, setlayout};
    layout = device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts.size(), layouts.data(), 1, &pcrange));
    
    
    
    auto computeShaderCode = Util::readFile("./resources/shaders/marchingcubes.comp.glsl.spv");
    vk::ShaderModule computeShader = device->createShaderModule(
        vk::ShaderModuleCreateInfo({}, computeShaderCode.size() * sizeof(char), reinterpret_cast<const uint32_t*>(computeShaderCode.data())));
    
    struct Constants {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t s_x;
        uint32_t s_y;
        uint32_t s_z;
    } constants;
    constants.x = local_size.x;
    constants.y = local_size.y;
    constants.z = local_size.z;
    constants.s_x = chunk::num_values.x;
    constants.s_y = chunk::num_values.y;
    constants.s_z = chunk::num_values.z;
    
    auto mapEntries = std::array {
        vk::SpecializationMapEntry(0, offsetof(Constants, x), sizeof(uint32_t)),
        vk::SpecializationMapEntry(1, offsetof(Constants, y), sizeof(uint32_t)),
        vk::SpecializationMapEntry(2, offsetof(Constants, z), sizeof(uint32_t)),
        vk::SpecializationMapEntry(3, offsetof(Constants, s_x), sizeof(uint32_t)),
        vk::SpecializationMapEntry(4, offsetof(Constants, s_y), sizeof(uint32_t)),
        vk::SpecializationMapEntry(5, offsetof(Constants, s_z), sizeof(uint32_t)),
    };
    
    auto specConstants = vk::SpecializationInfo(mapEntries.size(), mapEntries.data(), sizeof(Constants), &constants);
    
    pipeline = device->createComputePipeline(
        nullptr,
        vk::ComputePipelineCreateInfo(
            {},
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, computeShader, "main", &specConstants),
            layout
        )
    );
    
    device->destroy(computeShader);
    
    auto lookupData = Util::readFile("./resources/mclookup.bin");
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    lookupBuffer = VmaBuffer(device, &allocInfo, vk::BufferCreateInfo(
        {}, lookupData.size() * sizeof(char), vk::BufferUsageFlagBits::eUniformTexelBuffer,
        vk::SharingMode::eExclusive, 1, &device.c_i
    ));
    
    VmaAllocationInfo vai;
    vmaGetAllocationInfo(device, lookupBuffer, &vai);
    
    memcpy(vai.pMappedData, lookupData.data(), lookupBuffer.size);
    
    lookupView = device->createBufferView(vk::BufferViewCreateInfo({}, lookupBuffer, vk::Format::eR8Sint, 0, lookupBuffer.size));
    
    device->updateDescriptorSets({
        vk::WriteDescriptorSet(descSet, 0, 0, 1, vk::DescriptorType::eUniformTexelBuffer, nullptr, nullptr, &lookupView),
    }, {});
    
}

MCPipeline::~MCPipeline() {
    
    device->destroy(lookupView);
    
    device->destroy(pipeline);
    
    device->destroy(layout);
    
    device->destroy(descLayout);
    
    device->destroy(descPool);
    
}
