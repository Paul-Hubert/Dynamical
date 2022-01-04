#ifndef SYSTEM_H
#define SYSTEM_H

#include "entt/entt.hpp"

#include <iostream>

#include "vulkan/vulkan.h"
#include "extra/optick/optick.h"

namespace dy {
    
class System {
public:
    System(entt::registry& reg) : reg(reg) {};
    virtual void preinit() {}
    virtual void init() {};
    virtual void tick(float dt) = 0;
    void operator()(float dt) {
        tick(dt);
    }
    virtual void finish() {}
    virtual const char* name() = 0;
    virtual ~System() {}
    
    entt::registry& reg;
};

}

#endif
