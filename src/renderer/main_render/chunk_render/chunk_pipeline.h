#ifndef CHUNK_PIPELINE_H
#define CHUNK_PIPELINE_H

#include "renderer/vk.h"

#include "renderer/vmapp.h"

#include "glm/glm.hpp"

#include "renderer/num_frames.h"

class Device;
class Transfer;
class Swapchain;
class Renderpass;
class UBODescriptor;

constexpr int num_textures = 2;

class ChunkPipeline {
public:
    ChunkPipeline(Device& device, Transfer& transfer, Swapchain& swap, Renderpass& renderpass, UBODescriptor& ubo);
    ~ChunkPipeline();
    
    operator vk::Pipeline() { return pipeline; }
    operator vk::PipelineLayout() { return layout; }
    
    vk::DescriptorPool descPool;
    
    vk::DescriptorSetLayout materialLayout;
    vk::DescriptorSet materialSet;
    
    VmaImage materialTexture;
    vk::ImageView materialTextureView;
    
    vk::Sampler sampler;
    
    vk::PipelineLayout layout;
    vk::Pipeline pipeline;
    
    struct PC {
        glm::vec3 color;
    };
    
private:
    Device& device;
    Transfer& transfer;
    Swapchain& swap;
    Renderpass& renderpass;
    
};

#endif
