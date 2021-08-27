#ifndef RENDERER_H
#define RENDERER_H

#include "entt/entt.hpp"

#include "context/context.h"
#include "ui/ui.h"
#include "classic_render/classic_render.h"

class Renderer {
public:
    Renderer(entt::registry& reg);
    void preinit();
    void init();
    void prepare();
    void render();
    void finish();
    ~Renderer();
    
private:
    entt::registry& reg;
    Context ctx;
    UI ui;
    
    struct per_frame {
        vk::Semaphore transfer_semaphore;
    };
    std::vector<per_frame> per_frame;
    
};

#endif
