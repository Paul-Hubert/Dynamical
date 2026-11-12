#ifndef SYSTEM_H
#define SYSTEM_H

#include "entt/entt.hpp"

#include <cstdint>
#include <iostream>

// SystemSet opens a zone named after the system around each of its calls ; DY_ZONE_N("...") inside a system
// for the parts of it worth seeing apart
#include "util/profile.h"

namespace dy {

// When a system of a world's fixed lane runs within a decision window (simulation.md):
// Every tick, Act on the first tick only, Observe on the last tick only
enum class Phase : uint8_t { Every, Act, Observe };
    
class System {
public:
    System(entt::registry& reg) : reg(reg) {};
    virtual void preinit() {}
    virtual void init() {};
    virtual void tick(double dt) = 0;
    void operator()(double dt) {
        tick(dt);
    }
    virtual void finish() {}
    virtual const char* name() = 0;
    virtual ~System() {}
    
    entt::registry& reg;
};

}

#endif
