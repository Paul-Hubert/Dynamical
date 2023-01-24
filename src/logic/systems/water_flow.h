#ifndef WATER_FLOW_SYS_H
#define WATER_FLOW_SYS_H

#include "system.h"

#include <entt/entt.hpp>

namespace dy {

class WaterFlowSys : public System {
public:
    WaterFlowSys(entt::registry& reg) : System(reg) {};

    const char* name() override {
        return "WaterFlow";
    }

    void tick(double dt) override;
};

}

#endif
