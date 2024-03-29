#ifndef UISYS_H
#define UISYS_H

#include "system.h"

#include "entt/entt.hpp"

#include <imgui/imgui.h>
#include <SDL.h>

namespace dy {

class UISys : public System
{
public:
    UISys(entt::registry& reg);

    ~UISys() override;

    const char* name() override {
        return "UI";
    }

    void tick(double dt) override;
    
private:
    SDL_Cursor* g_MouseCursors[ImGuiMouseCursor_COUNT] = {0};
    Uint64 g_Time = 0;

};

}

#endif // UISYS_H
