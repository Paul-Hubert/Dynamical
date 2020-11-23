#ifndef RENDERER_H
#define RENDERER_H

#include "camera.h"
#include "vr_render/vr_render.h"

#include "logic/systems/system.h"

#include "entt/entt.hpp"

class Context;

class Renderer : public System {
public:
    Renderer(entt::registry& reg);
    void preinit() override;
    void init() override;
    void tick() override;
    const char* name() override {return "Renderer";};
    void finish() override;
    ~Renderer() override;
    
private:
    std::unique_ptr<Context> ctx;
    VRRender vr_render;
};

#endif
