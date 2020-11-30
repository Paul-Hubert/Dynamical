#ifndef SYSTEM_H
#define SYSTEM_H

#include "entt/entt.hpp"

#include <iostream>

#include "optick.h"

class System {
public:
    System(entt::registry& reg) : reg(reg) {};
    virtual void preinit() {}
    virtual void init() = 0;
    virtual void tick() = 0;
    void operator()() {
        tick();
    }
    virtual void finish() {}
    virtual const char* name() = 0;
    virtual ~System() {}
    
    entt::registry& reg;
};

#endif
