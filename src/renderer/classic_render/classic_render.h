#ifndef CLASSIC_RENDER_H
#define CLASSIC_RENDER_H

#include <entt/entt.hpp>
#include "renderpass.h"

#include "renderer/util/vmapp.h"
#include "renderer/context/num_frames.h"

#include "renderer/scene_pass.h"
#include "renderer/view_ubo.h"
#include "renderer/lighting/sun_visibility.h"

#include <memory>

#include <vector>

namespace dy {

class Context;

// Desktop window render target : swapchain, depth buffers, one command buffer and view UBO per frame
class ClassicRender {
public:
    ClassicRender(Context& ctx, entt::registry& reg);
    ~ClassicRender();

    // The pass the scene passes draw in, and the one of the image the target shows (the same without ray tracing)
    vk::RenderPass getRenderpass() {
        return lit ? lit->scenePass() : renderpass.renderpass;
    }
    vk::RenderPass getFinalRenderpass() {
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

    // Copies this frame's image to host memory at the end of record(). Read it once the frame fence is signaled
    bool requestCapture();
    void readCapture(std::vector<uint8_t>& rgb, uint32_t& width, uint32_t& height);

private:
    void recordCapture(vk::CommandBuffer command);

    entt::registry& reg;
    Context& ctx;

    ClRenderpass renderpass;

    // With ray tracing : the images the scene is drawn in, and the sun's visibility
    std::unique_ptr<SunVisibility> lit;

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

    glm::mat4 previous_view_projection = glm::mat4(1.f);
    glm::vec3 previous_position = glm::vec3(0.f);
    bool have_previous = false;

    VmaBuffer capture_buffer;
    bool capture_requested = false;

    // gpu_times=true : a timestamp before the first scene pass and after each one, read back once the
    // frame is done. Means are logged at every capture and every 300 frames
    static constexpr uint32_t max_timed_passes = 15;
    struct PassTimes {
        vk::QueryPool pool;                 // max_timed_passes + 1 queries per frame
        double period = 0.;                 // nanoseconds per tick
        uint64_t mask = ~0ull;              // valid bits of a timestamp
        std::vector<uint32_t> written;      // per frame : passes timed in its command buffer
        std::vector<const char*> names;
        std::vector<double> sums;           // milliseconds, per pass
        uint32_t frames = 0;
    } times;
    void readPassTimes(uint32_t frame_index);
    void logPassTimes();

};

}

#endif
