#ifndef MC_PIPELINE_H
#define MC_PIPELINE_H

#include "renderer/vk.h"

#include "renderer/vmapp.h"

#include "glm/glm.hpp"

constexpr glm::ivec3 local_size(8, 4, 8);

struct MCPushConstants {
    glm::ivec3 pos;
    int size;
    float time;
};

class Device;
class Terrain;

class MCPipeline {
public:
    MCPipeline(Device& device, vk::DescriptorSetLayout setlayout);
    ~MCPipeline();
    
    operator vk::Pipeline() { return pipeline; }
    operator VkPipeline() { return static_cast<VkPipeline>(pipeline); }
    operator vk::PipelineLayout() { return layout; }
    operator VkPipelineLayout() { return static_cast<VkPipelineLayout>(layout); }
    operator vk::DescriptorSet() { return descSet; }
    operator VkDescriptorSet() { return static_cast<VkDescriptorSet>(descSet); }
    
private:
    
    Device& device;
    
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;
    
    VmaBuffer lookupBuffer;
    vk::BufferView lookupView;
    
};

#endif
