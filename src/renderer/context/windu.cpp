#include "windu.h"

#include <iostream>
#include <vector>
#include "util/util.h"
#include "util/log.h"

#include <SDL_vulkan.h>
#include "renderer/util/vk.h"
#include "context.h"

using namespace dy;

Windu::Windu(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {
    
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);



    window = SDL_CreateWindow("Dynamical",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              -1, -1,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
             );
    
    if (window == nullptr) {
        std::cout << "Could not create SDL window: " << SDL_GetError() << std::endl;
    }

    SDL_StartTextInput();

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

    SDL_StopTextInput();
    
    SDL_DestroyWindow(window);

    SDL_Quit();
}
