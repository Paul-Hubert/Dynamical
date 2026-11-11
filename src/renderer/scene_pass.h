#ifndef SCENE_PASS_H
#define SCENE_PASS_H

#include "renderer/util/vk.h"

#include <array>
#include <cstdint>

namespace dy {

// The desktop window and the headset are two independent render targets.
// Each has its own render pass, framebuffers, command buffers and view UBOs,
// and both record the same scene passes every frame (in parallel).
enum class RenderTarget : uint32_t {
    Desktop = 0,
    VR = 1,
};
constexpr uint32_t NUM_RENDER_TARGETS = 2;

inline uint32_t index(RenderTarget target) {
    return static_cast<uint32_t>(target);
}

struct RenderView {
    RenderTarget target;
    uint32_t frame_index;
    uint32_t eye;               // 0 on desktop
    vk::DescriptorSet view_set; // set 0 : ViewUBO
    vk::Extent2D extent;
};

class ScenePass {
public:
    virtual ~ScenePass() = default;

    virtual const char* name() const = 0;

    virtual bool supports(RenderTarget target) const { return true; }

    // Drawn over the finished picture, in the render pass of the image the target shows (the UI). The others
    // are the scene : with ray tracing they draw in the pass of SunVisibility, which has two colour attachments
    virtual bool overlay() const { return false; }

    // Called once per render target. Pipelines differ between targets since the color formats do.
    virtual void createPipeline(RenderTarget target, vk::RenderPass renderpass) = 0;

    // CPU side work, once per frame, on the main thread, before any recording.
    virtual void update(uint32_t frame_index) {}

    // Records inside an active render pass with viewport and scissor already set.
    // Desktop and VR record concurrently : only read shared state here.
    virtual void record(vk::CommandBuffer command, const RenderView& view) = 0;

protected:
    std::array<vk::Pipeline, NUM_RENDER_TARGETS> pipelines {};
};

}

#endif
