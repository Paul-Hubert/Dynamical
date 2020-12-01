#ifndef MAIN_RENDER_H
#define MAIN_RENDER_H

#include <vector>
#include <taskflow/taskflow.hpp>
#include <entt/entt.hpp>

#include "renderpass.h"
#include "object_render.h"
#include "ui_render.h"
#include "view_ubo.h"
#include "material_manager.h"

#include "renderer/util/vmapp.h"
#include "renderer/context/num_frames.h"

#include "renderer/util/xr.h"

class Context;
class Camera;

class VRRender {
public:
    VRRender(entt::registry& reg, Context& ctx);

    void record(vk::CommandBuffer command);
    void prepare();
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

    vk::CommandPool commandPool;
    
    std::vector<uint32_t> swapchain_image_indices;

    uint32_t frame_index = 0;
    struct per_frame {
        vk::CommandBuffer commandBuffer;
        vk::Fence fence;
    };
    std::vector<per_frame> per_frame;

    uint32_t swapchain_index = 0;
    struct per_swapchain_image {
        vk::Semaphore acquire_semaphore;
        vk::Semaphore present_semaphore;
        vk::CommandBuffer commandBuffer;
        vk::Fence fence;
    };
    std::vector<per_swapchain_image> per_swapchain_image;

};

#endif
