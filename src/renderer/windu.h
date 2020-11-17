#ifndef WINDU_H
#define WINDU_H

#include <SDL.h>
#include "renderer/vk.h"

class Windu {
public:
    Windu();
    ~Windu();
    operator SDL_Window*() { return window; }
    operator vk::SurfaceKHR() { return surface; }
    operator VkSurfaceKHR() { return static_cast<VkSurfaceKHR>(surface); }
    int getWidth();
    int getHeight();
    bool resize();
    vk::SurfaceKHR surface;
    
private:
    int width;
    int height;
    SDL_Window* window;
};
   
#endif
