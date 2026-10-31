#ifndef SYSTEM_LIST_H
#define SYSTEM_LIST_H

#include "entt/entt.hpp"

#include "logic/systems/system.h"

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

// base: a ring of boxes dropped on the marching cubes terrain (init)
DEFINE_SYSTEM(BoxRingSys)

// base: the glTF files of resources/models lined up behind the spawn point (init)
DEFINE_SYSTEM(ModelLineupSys)

// base: left click throws a box from the desktop camera
DEFINE_SYSTEM(BoxThrowerSys)

// ImGui window : renderer toggles, grass parameters, VR state, chunk statistics
DEFINE_SYSTEM(DebugMenuSys)

}

#undef DEFINE_SYSTEM
#endif
