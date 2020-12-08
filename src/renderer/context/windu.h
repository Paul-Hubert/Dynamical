#ifndef WINDU_H
#define WINDU_H

#include <SDL.h>
#include "renderer/util/vk.h"

class Context;

class Windu {
public:
    Windu(Context& ctx);
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

    int width;
    int height;
    SDL_Window* window;
};
   
#endif
