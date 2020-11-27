#ifndef MAIN_RENDER_H
#define MAIN_RENDER_H

#include "renderpass.h"
#include "object_render.h"
#include "ui_render.h"
#include "view_ubo.h"
#include "material_manager.h"

#include <taskflow/taskflow.hpp>

#include "renderer/vmapp.h"

#include "entt/entt.hpp"

#include "renderer/num_frames.h"

#define XR_USE_GRAPHICS_API_VULKAN
#if defined(WIN32)
#define XR_USE_PLATFORM_WIN32
#include <Windows.h>
#else
#define XR_USE_PLATFORM_XLIB
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <vector>

class Context;
class Camera;

class VRRender {
public:
    VRRender(entt::registry& reg, Context& ctx);

    void record(vk::CommandBuffer command);
    void render(std::vector<vk::Semaphore> waits, std::vector<vk::Semaphore> signals);

    ~VRRender();
    
private:
    entt::registry& reg;
    Context& ctx;
    Renderpass renderpass;
    ViewUBO ubo;
    MaterialManager material_manager;
    ObjectRender object_render;
    UIRender ui_render;

    uint32_t frame_index = 0;
    std::vector<uint32_t> swapchain_image_indices;
    bool ready = false;
    
    vk::CommandPool commandPool;
    std::array<vk::CommandBuffer, NUM_FRAMES> commandBuffers;
    vk::Fence fence;

    tf::Taskflow taskflow;
    std::future<void> future;

};

#endif
