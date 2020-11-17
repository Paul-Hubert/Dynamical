#ifndef MAIN_RENDER_H
#define MAIN_RENDER_H

#include "renderpass.h"
#include "object_render.h"
#include "ui_render.h"
#include "ubo_descriptor.h"

#include "renderer/vmapp.h"

#include "entt/entt.hpp"

#include "renderer/num_frames.h"

#include <vector>

class Context;
class Camera;

class MainRender {
public:
    MainRender(entt::registry& reg, Context& ctx);
    void setup();
    void render(uint32_t index, Camera& camera, std::vector<vk::Semaphore> waits, std::vector<vk::Semaphore> signals);
    void cleanup();
    ~MainRender();
    
private:
    entt::registry& reg;
    Context& ctx;
    Renderpass renderpass;
    UBODescriptor ubo;
    ObjectRender object_render;
    UIRender ui_render;
    
    vk::CommandPool commandPool;
    std::array<vk::CommandBuffer, NUM_FRAMES> commandBuffers;
    std::array<vk::Fence, NUM_FRAMES> fences;
    
};

#endif
