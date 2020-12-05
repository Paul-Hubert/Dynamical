#ifndef UI_H
#define UI_H

#include "entt/entt.hpp"

#include "imgui.h"
#include <sdl2/SDL.h>

class UI {
public:
    UI(entt::registry& reg);
    void prepare();
    void render();
    ~UI();
private:
    entt::registry& reg;
    SDL_Cursor* g_MouseCursors[ImGuiMouseCursor_COUNT] = {0};
    Uint64 g_Time = 0;
};

#endif
