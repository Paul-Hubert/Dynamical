#ifndef CLASSIC_RENDER_H
#define CLASSIC_RENDER_H

#include <entt/entt.hpp>
#include "renderpass.h"

#include "renderer/util/vmapp.h"
#include "renderer/context/num_frames.h"

#include "renderer/view_ubo.h"

class Context;
class Camera;

class ClassicRender {
public:
    ClassicRender(entt::registry& reg, Context& ctx, vk::DescriptorSetLayout set_layout);

    void prepare(uint32_t frame_index, std::function<void(vk::CommandBuffer)>& recorder, vk::PipelineLayout pipeline_layout);
    void render(uint32_t frame_index);

    vk::RenderPass getRenderpass() {
        return renderpass;
    }

    ~ClassicRender();

private:
    entt::registry& reg;
    Context& ctx;
    ClRenderpass renderpass;

    vk::CommandPool commandPool;

    vk::DescriptorPool descriptorPool;

    struct per_frame {
        vk::CommandBuffer commandBuffer;
        vk::Fence fence;
        vk::DescriptorSet set;
        VmaBuffer ubo;
        ViewUBO* pointer;
        vk::Semaphore acquireSemaphore;
        vk::Semaphore presentSemaphore;
    };
    std::vector<per_frame> per_frame;

};

#endif
