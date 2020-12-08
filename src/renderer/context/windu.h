#ifndef WINDU_H
#define WINDU_H

#include <SDL.h>
#include "renderer/util/vk.h"

#include <entt/entt.hpp>

class Context;

class Windu {
public:
    Windu(Context& ctx, entt::registry& reg);
    ~Windu();
    operator SDL_Window*() { return window; }
    operator vk::SurfaceKHR() { return surface; }
    operator VkSurfaceKHR() { return static_cast<VkSurfaceKHR>(surface); }
    int getWidth();
    int getHeight();
    bool resize();
    vk::SurfaceKHR surface;
    
private:
    Context& ctx;
    entt::registry& reg;

    int width;
    int height;
    SDL_Window* window;
};
   
#endif
