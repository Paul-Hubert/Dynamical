#ifndef UI_RENDER_H
#define UI_RENDER_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include "imgui.h"

#include <memory>

#include "renderer/model/imagec.h"

class Context;
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
    UIRender(Context& ctx, vk::RenderPass renderpass);
    ~UIRender();
    
    void createOrResizeBuffer(vk::Buffer& buffer, vk::DeviceMemory& buffer_memory, vk::DeviceSize& p_buffer_size, size_t new_size,vk::BufferUsageFlagBits usage);
    
    void render(vk::CommandBuffer commandBuffer, uint32_t index);
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline graphicsPipeline;
    
    std::vector<FrameDataForRender> g_FramesDataBuffers;
    
    std::shared_ptr<ImageC> fontAtlas;
    vk::ImageView fontView;
    vk::Sampler fontSampler;
    
private:
    
    Context& ctx;
    
    void initPipeline(vk::RenderPass);
    
};

#endif
