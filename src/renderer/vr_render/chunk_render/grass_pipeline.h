#ifndef GRASS_PIPELINE_H
#define GRASS_PIPELINE_H

#include "renderer/vk.h"

#include "renderer/vmapp.h"

#include "glm/glm.hpp"

#include "renderer/num_frames.h"

class Device;
class Transfer;
class Swapchain;
class Renderpass;
class ViewUBO;

struct GrassPC {
    float base_normal[4];
    float tile_size;
    float grass_height;
    float noise_frequency;
    float noise_amplitude;
};

class GrassPipeline {
public:
    GrassPipeline(Device& device, Transfer& transfer, Swapchain& swap, Renderpass& renderpass, ViewUBO& ubo);
    void makeDebugWindow();
    ~GrassPipeline();
    
    operator vk::Pipeline() { return pipeline; }
    operator vk::PipelineLayout() { return layout; }
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;
    
    vk::Sampler sampler;

    VmaImage raycastImage;
    vk::ImageView raycastImageView;

    vk::Format noiseFormat;
    VmaImage noiseImage;
    vk::ImageView noiseImageView;
    
    vk::PipelineLayout layout;
    vk::Pipeline pipeline;
    
    GrassPC pc;
    
private:
    Device& device;
    Transfer& transfer;
    Swapchain& swap;
    Renderpass& renderpass;
    
};

#endif
