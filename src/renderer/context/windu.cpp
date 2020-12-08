#include "windu.h"

#include <iostream>
#include <vector>
#include "util/util.h"

#include <SDL_vulkan.h>
#include "renderer/util/vk.h"
#include "context.h"

Windu::Windu(Context& ctx) : ctx(ctx) {
    
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    
    SDL_DisplayMode mode;
    
    SDL_GetCurrentDisplayMode(0, &mode);
    
    window = SDL_CreateWindow("Dynamical",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              mode.w, mode.h,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN
             );
    
    if (window == nullptr) {
        std::cout << "Could not create SDL window: " << SDL_GetError() << std::endl;
    }
    
    SDL_GetWindowSize(window, &width, &height);
    
}

int Windu::getHeight() {
    return height;
}

int Windu::getWidth() {
    return width;
}

bool Windu::resize() {
    
    int w,h;
    SDL_GetWindowSize(window, &w, &h);
    
    if(w == width && h == height) {
        return false;
    }
    
    width = w;
    height = h;
    
    return true;
    
}

Windu::~Windu() {
    
    SDL_DestroyWindow(window);

    SDL_Quit();
}
