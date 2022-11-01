#ifndef RENDERER_H
#define RENDERER_H

#include "entt/entt.hpp"

#include "context/context.h"
#include "classic_render/classic_render.h"

namespace dy {
    
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
    
    struct per_frame {
        vk::Semaphore transfer_semaphore;
        vk::Semaphore compute_semaphore;
        vk::Semaphore graphics_semaphore;
    };
    std::vector<per_frame> per_frame;

    vk::Semaphore last_frame_semaphore = VK_NULL_HANDLE;
    
};

}

#endif
