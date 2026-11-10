#ifndef RENDERER_H
#define RENDERER_H

#include "entt/entt.hpp"

#include "context/context.h"
#include "classic_render/classic_render.h"
#include "vr_render/vr_render.h"
#include "scene_pass.h"
#include "tree_shade.h"
#include "scene/scene_def.h"

#include <chrono>
#include <memory>
#include <vector>

namespace dy {

// Owns the two render targets (desktop window and headset) and the scene passes they share.
//
// Frame : prepare() waits the frame fence, acquires the desktop image and begins the OpenXR frame.
// Systems tick (marching cubes compute is recorded there). render() then submits
// transfer -> compute -> graphics, the desktop and VR command buffers being recorded in parallel.
class Renderer {
public:
    // Builds the passes the scene lists, in draw order
    Renderer(entt::registry& reg, const RendererDef& desc);
    void preinit();
    void init();
    void prepare();
    void render();
    void finish();
    ~Renderer();

    Context& getContext() { return ctx; }

    // Capture: request before render(), then take after it (waits for this frame's fence). RGB, 8 bits
    bool requestCapture();
    bool takeCapture(std::vector<uint8_t>& rgb, uint32_t& width, uint32_t& height);

private:
    entt::registry& reg;
    Context ctx;

    std::unique_ptr<TreeShade> tree_shade;
    std::unique_ptr<ClassicRender> classic;
    std::unique_ptr<VRRender> vr;

    std::vector<std::unique_ptr<ScenePass>> passes;
    std::vector<ScenePass*> pass_list;

    struct per_frame {
        vk::Fence fence;
        vk::Semaphore transfer_semaphore;
        vk::Semaphore compute_semaphore;
    };
    std::vector<per_frame> per_frame;

    std::chrono::steady_clock::time_point start_time;

};

}

#endif
