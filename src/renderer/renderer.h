#ifndef RENDERER_H
#define RENDERER_H

#include "windu.h"
#include "instance.h"
#include "device.h"
#include "transfer.h"
#include "swapchain.h"
#include "camera.h"
#include "main_render/main_render.h"

#include "logic/systems/system.h"

#include "entt/entt.hpp"

class Renderer : public System {
public:
    Renderer(entt::registry& reg);
    void preinit() override;
    void init() override;
    void tick() override;
    const char* name() override {return "Renderer";};
    void finish() override;
    ~Renderer() override;
    
    void resize();
    
private:
    Windu win;
    Instance instance;
    Device device;
    Transfer transfer;
    Swapchain swap;
    Camera camera;
    MainRender main_render;
    
    int frame_num = 0;
    int semindex = 0;
    std::vector<vk::Semaphore> waitsems;
    std::vector<vk::Semaphore> signalsems;
    std::vector<vk::Semaphore> computesems;
};

#endif
