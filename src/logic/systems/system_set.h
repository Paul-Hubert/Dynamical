#ifndef SYSTEM_SET_H
#define SYSTEM_SET_H

#include <list>

#include "system.h"
#include "entt/entt.hpp"

class SystemSet {
public:
    SystemSet(entt::registry& reg) : reg(reg) {}
    
    void add(System* system);
    
    void preinit();
    
    void init();
    
    void tick();
    
    void finish();

    ~SystemSet();
    
private:
    std::list<System*> systems;
    
    entt::registry& reg;
};

#endif
