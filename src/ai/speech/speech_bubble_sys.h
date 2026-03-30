#pragma once
#include "logic/systems/system.h"
#include <entt/entt.hpp>

namespace dy {

class SpeechBubbleSys : public System {
public:
    SpeechBubbleSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "SpeechBubble"; }
    void tick(double dt) override;
};

}
