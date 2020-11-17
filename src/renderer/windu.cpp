#include "windu.h"

#include <iostream>
#include <vector>

#include <SDL_vulkan.h>
#include "renderer/vk.h"

Windu::Windu() {
    
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    
    SDL_DisplayMode mode;
    
    SDL_GetCurrentDisplayMode(0, &mode);
    
    window = SDL_CreateWindow("Dynamical",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1920, 1080,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN
             );
    
    if (window == nullptr) {
        std::cout << "Could not create SDL window: " << SDL_GetError() << std::endl;
    }
    
    SDL_ShowCursor(SDL_FALSE);
    
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
