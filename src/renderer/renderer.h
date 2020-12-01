#ifndef RENDERER_H
#define RENDERER_H

#include "vr_render/vr_render.h"

#include "entt/entt.hpp"

class Context;

class Renderer {
public:
    Renderer(entt::registry& reg);
    void preinit();
    void init();
    void render();
    void finish();
    ~Renderer();
    
private:
    entt::registry& reg;
    std::unique_ptr<Context> ctx;
    VRRender vr_render;
};

#endif
