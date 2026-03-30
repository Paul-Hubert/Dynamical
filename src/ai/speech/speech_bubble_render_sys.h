#pragma once

#include <logic/systems/system.h>

namespace dy {
    class SpeechBubbleRenderSys : public System {
    public:
        SpeechBubbleRenderSys(entt::registry& reg) : System(reg) {};
        void preinit() override {}
        void init() override {}
        void tick(double dt) override;
        void finish() override {}
        const char* name() override { return "SpeechBubbleRenderSys"; }
    };
}
