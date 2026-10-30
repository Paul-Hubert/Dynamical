#ifndef CLASSIC_RENDER_H
#define CLASSIC_RENDER_H

#include <entt/entt.hpp>
#include "renderpass.h"

#include "renderer/util/vmapp.h"
#include "renderer/context/num_frames.h"

#include "renderer/scene_pass.h"
#include "renderer/view_ubo.h"

#include <vector>

namespace dy {

class Context;

// Desktop window render target : swapchain, depth buffers, one command buffer and view UBO per frame
class ClassicRender {
public:
    ClassicRender(Context& ctx, entt::registry& reg);
    ~ClassicRender();

    vk::RenderPass getRenderpass() {
        return renderpass;
    }

    // Acquires a swapchain image, false if there is nothing to draw to this frame (minimized window)
    bool prepare(uint32_t frame_index);
    bool active() const { return acquired; }

    void updateView(uint32_t frame_index, float time);
    void record(uint32_t frame_index, const std::vector<ScenePass*>& passes);

    vk::CommandBuffer getCommandBuffer(uint32_t frame_index) { return per_frame[frame_index].commandBuffer; }
    vk::Semaphore getAcquireSemaphore(uint32_t frame_index) { return per_frame[frame_index].acquireSemaphore; }
    vk::Semaphore getPresentSemaphore();

    void present();

private:
    entt::registry& reg;
    Context& ctx;

    ClRenderpass renderpass;

    vk::CommandPool commandPool;
    vk::DescriptorPool descriptorPool;

    struct per_frame {
        vk::CommandBuffer commandBuffer;
        vk::DescriptorSet set;
        VmaBuffer ubo;
        ViewUBO* pointer;
        vk::Semaphore acquireSemaphore;
    };
    std::vector<per_frame> per_frame;

    // One per swapchain image : the presentation engine may still hold it when the frame index wraps
    std::vector<vk::Semaphore> presentSemaphores;

    bool acquired = false;

};

}

#endif
