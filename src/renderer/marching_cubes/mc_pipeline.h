#ifndef MC_PIPELINE_H
#define MC_PIPELINE_H

#include "vulkan/vulkan.hpp"

#include "renderer/vmapp.h"

class Device;
class Terrain;

class MCPipeline {
public:
    MCPipeline(Device& device, Terrain& terrain);
    ~MCPipeline();
    
    operator vk::Pipeline() { return pipeline; }
    operator VkPipeline() { return static_cast<VkPipeline>(pipeline); }
    operator vk::PipelineLayout() { return layout; }
    operator VkPipelineLayout() { return static_cast<VkPipelineLayout>(layout); }
    operator vk::DescriptorSet() { return descSet; }
    operator VkDescriptorSet() { return static_cast<VkDescriptorSet>(descSet); }
    
private:
    Device& device;
    Terrain& terrain;
    
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;
    
    VmaBuffer lookupBuffer;
    vk::BufferView lookupView;
    
};

#endif