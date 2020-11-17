#ifndef RENDERER_H
#define RENDERER_H

#include "context.h"
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
    std::unique_ptr<Context> ctx;
    Camera camera;
    MainRender main_render;

    std::vector<vk::Semaphore> waitsems;
    std::vector<vk::Semaphore> signalsems;
    std::vector<vk::Semaphore> computesems;
};

#endif
