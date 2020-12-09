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
    void tick(float dt) override; \
    void finish() override; \
    const char* name() override { return #NAME; }; \
};

DEFINE_SYSTEM(VRPlayerControlSys)

DEFINE_SYSTEM(SpectatorControlSys)

#undef DEFINE_SYSTEM
#endif