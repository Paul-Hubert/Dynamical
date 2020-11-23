#ifndef MAIN_RENDER_H
#define MAIN_RENDER_H

#include "renderpass.h"
#include "object_render.h"
#include "ui_render.h"
#include "view_ubo.h"

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

    void render(std::vector<vk::Semaphore> waits, std::vector<vk::Semaphore> signals);

    ~VRRender();
    
private:
    entt::registry& reg;
    Context& ctx;
    Renderpass renderpass;
    ViewUBO ubo;
    ObjectRender object_render;
    UIRender ui_render;
    
    vk::CommandPool commandPool;
    vk::CommandBuffer commandBuffer;
    vk::Fence fence;

};

#endif
