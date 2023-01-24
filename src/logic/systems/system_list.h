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

DEFINE_SYSTEM(CameraSys)

DEFINE_SYSTEM(TimeSys)

DEFINE_SYSTEM(DevMenuSys)

DEFINE_SYSTEM(ChunkGenerationSys)

DEFINE_SYSTEM(PatherSys)

DEFINE_SYSTEM(BasicNeedsSys)

DEFINE_SYSTEM(HarvestSys)

DEFINE_SYSTEM(EatSys)

DEFINE_SYSTEM(ActionBarSys)

DEFINE_SYSTEM(MapConfiguratorSys)

DEFINE_SYSTEM(MapEditorSys)

}

#undef DEFINE_SYSTEM
#endif
