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
    void tick() override; \
    void finish() override; \
    const char* name() override { return #NAME; }; \
};

DEFINE_SYSTEM(InputSys)

DEFINE_SYSTEM(VRInputSys)

DEFINE_SYSTEM(DebugSys)

DEFINE_SYSTEM(PlayerControlSys)

DEFINE_SYSTEM(VRPlayerControlSys)

DEFINE_SYSTEM(ChunkManagerSys)

DEFINE_SYSTEM(ChunkLoaderSys)

DEFINE_SYSTEM(ChunkGeneratorSys)

DEFINE_SYSTEM(ChunkSys)

#undef DEFINE_SYSTEM
#endif