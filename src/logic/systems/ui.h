#ifndef UI_H
#define UI_H

#include "system.h"

#include "imgui.h"
#include <SDL.h>

class UISys : public System {
public:
    UISys(entt::registry& reg);
    void init() override;
    void tick() override;
    ~UISys() override;
    const char* name() override {return "UI";};
private:
    SDL_Cursor* g_MouseCursors[ImGuiMouseCursor_COUNT] = {0};
    Uint64 g_Time = 0;
};

#endif
