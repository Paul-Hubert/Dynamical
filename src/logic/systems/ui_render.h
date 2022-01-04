#ifndef UI_RENDER_SYS_H
#define UI_RENDER_SYS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include "imgui.h"

#include "system.h"

#include <entt/entt.hpp>

#include <memory>

namespace dy {

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

class UIRenderSys : public System {
public:
    UIRenderSys(entt::registry& reg);
    ~UIRenderSys() override;
    
    const char* name() override {
        return "UIRender";
    }

    void init() override;
    void tick(float dt) override;
    
    void createOrResizeBuffer(vk::Buffer& buffer, vk::DeviceMemory& buffer_memory, vk::DeviceSize& p_buffer_size, size_t new_size,vk::BufferUsageFlagBits usage);
    
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline graphicsPipeline;
    
    std::vector<FrameDataForRender> g_FramesDataBuffers;
    
    std::shared_ptr<VmaImage> fontAtlas;
    vk::ImageView fontView;
    vk::Sampler fontSampler;
    
private:
    
    void initPipeline(vk::RenderPass);
    
};

}

#endif

