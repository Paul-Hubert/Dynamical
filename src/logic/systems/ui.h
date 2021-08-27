#ifndef UISYS_H
#define UISYS_H

#include "system.h"

#include "entt/entt.hpp"

#include "imgui.h"
#include <SDL.h>

class UISys : public System
{
public:
    UISys(entt::registry& reg);

    ~UISys() override;

    const char* name() override {
        return "UI";
    }

    void tick(float dt) override;

};

#endif // UISYS_H
