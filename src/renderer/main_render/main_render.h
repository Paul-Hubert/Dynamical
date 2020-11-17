#ifndef MAIN_RENDER_H
#define MAIN_RENDER_H

#include "renderpass.h"
#include "ui_render.h"
#include "ubo_descriptor.h"

#include "renderer/vmapp.h"

#include "entt/entt.hpp"

#include "renderer/num_frames.h"

#include <vector>

class Instance;
class Transfer;
class Device;
class Swapchain;
class Camera;
class Terrain;

class MainRender {
public:
    MainRender(Instance& instance, Device& device, Transfer& transfer, Swapchain& swap, Camera& camera);
    void setup();
    void render(entt::registry& reg, uint32_t index, std::vector<vk::Semaphore> waits, std::vector<vk::Semaphore> signals);
    void cleanup();
    ~MainRender();
    
private:
    Renderpass renderpass;
    UBODescriptor ubo;
    UIRender ui_render;
    Instance& instance;
    Device& device;
    Transfer& transfer;
    Swapchain& swap;
    Camera& camera;
    
    vk::CommandPool commandPool;
    std::array<vk::CommandBuffer, NUM_FRAMES> commandBuffers;
    std::array<vk::Fence, NUM_FRAMES> fences;
    
};

#endif
