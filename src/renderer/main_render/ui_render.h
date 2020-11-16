#ifndef UI_RENDER_H
#define UI_RENDER_H

#include "vulkan/vulkan.hpp"
#include "renderer/vmapp.h"

#include "imgui.h"

#include "renderer/num_frames.h"

class Device;
class Transfer;
class Swapchain;
class Renderpass;

// Frame data
class FrameDataForRender {
public:
    vk::DeviceMemory  vertexBufferMemory;
    vk::DeviceMemory  indexBufferMemory;
    vk::DeviceSize    vertexBufferSize;
    vk::DeviceSize    indexBufferSize;
    vk::Buffer        vertexBuffer;
    vk::Buffer        indexBuffer;
};

class UIRender {
public:
    UIRender(Device &win, Swapchain& swap, Transfer& transfer, Renderpass& renderpass);
    ~UIRender();
    
    void createOrResizeBuffer(vk::Buffer& buffer, vk::DeviceMemory& buffer_memory, vk::DeviceSize& p_buffer_size, size_t new_size,vk::BufferUsageFlagBits usage);
    
    void render(vk::CommandBuffer commandBuffer, uint32_t i);
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline graphicsPipeline;
    
    std::array<FrameDataForRender, NUM_FRAMES> g_FramesDataBuffers;
    
    VmaImage fontAtlas;
    vk::ImageView fontView;
    vk::Sampler fontSampler;
    
private:
    
    Device& device;
    Swapchain& swap;
    Transfer& transfer;
    
    void initPipeline(vk::RenderPass);
    
};

#endif
