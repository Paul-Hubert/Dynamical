#ifndef VR_RENDER_H
#define VR_RENDER_H

#include <vector>
#include <entt/entt.hpp>

#include "renderpass.h"
#include "vr_input.h"

#include "renderer/util/vmapp.h"
#include "renderer/util/xr.h"
#include "renderer/context/num_frames.h"

#include "renderer/scene_pass.h"
#include "renderer/view_ubo.h"

namespace dy {

class Context;

// Headset render target : OpenXR frame loop, one swapchain and view UBO per eye.
// Runs alongside the desktop target every frame, it does not replace it.
class VRRender {
public:
    VRRender(Context& ctx, entt::registry& reg);
    ~VRRender();

    bool enabled() const;

    vk::RenderPass getRenderpass() {
        return renderpass;
    }

    // Handles session events, then xrWaitFrame / xrBeginFrame when the session runs
    void prepare(uint32_t frame_index);

    // A frame was begun and the runtime wants pixels
    bool active() const { return frame_begun && should_render; }

    // Main thread, before recording : acquires the eye swapchain images
    void acquire();
    void updateView(uint32_t frame_index, float time);
    void record(uint32_t frame_index, const std::vector<ScenePass*>& passes);

    vk::CommandBuffer getCommandBuffer(uint32_t frame_index) { return per_frame[frame_index].commandBuffer; }

    // After the GPU submission : releases images and ends the frame (also when nothing was rendered)
    void endFrame();

private:
    void pollEvents();

    entt::registry& reg;
    Context& ctx;

    VRRenderpass renderpass;
    std::unique_ptr<VRActions> actions;

    vk::CommandPool commandPool;
    vk::DescriptorPool descriptorPool;

    struct per_frame {
        vk::CommandBuffer commandBuffer;
        struct per_eye {
            vk::DescriptorSet set;
            VmaBuffer ubo;
            ViewUBO* pointer;
        };
        std::vector<per_eye> eyes;
    };
    std::vector<per_frame> per_frame;

    bool session_running = false;
    bool frame_begun = false;
    bool should_render = false;
    XrFrameState frame_state { XR_TYPE_FRAME_STATE };
    std::vector<XrView> views;
    std::vector<uint32_t> image_indices;

};

}

#endif
