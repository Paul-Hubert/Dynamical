#ifndef SYSTEM_LIST_H
#define SYSTEM_LIST_H

#include "entt/entt.hpp"

#include "system.h"

#define DEFINE_SYSTEM(NAME) \
class NAME : public System { \
public: \
    NAME(entt::registry& reg) : System(reg) {}; \
    void preinit() override; \
    void init() override; \
    void tick(double dt) override; \
    void finish() override; \
    const char* name() override { return #NAME; }; \
};

namespace dy {

DEFINE_SYSTEM(TimeSys)

// Free flying desktop camera
DEFINE_SYSTEM(PlayerControlSys)

// VR locomotion and controller models
DEFINE_SYSTEM(VRControlSys)

// Demo glTF objects placed on the marching cubes terrain
DEFINE_SYSTEM(SceneSys)

// ImGui window : renderer toggles, grass parameters, VR state, chunk statistics
DEFINE_SYSTEM(DebugMenuSys)

}

#undef DEFINE_SYSTEM
#endif
