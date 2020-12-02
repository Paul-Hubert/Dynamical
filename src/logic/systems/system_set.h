#ifndef SYSTEM_SET_H
#define SYSTEM_SET_H

#include <vector>
#include <memory>

#include "system.h"
#include "entt/entt.hpp"

class SystemSet {
public:
    SystemSet(entt::registry& reg) : reg(reg) {}
    
    template<typename T, typename std::enable_if<std::is_base_of<System, T>::value>::type* = nullptr>
    void add() {
        systems.push_back(std::make_unique<T>(reg));
    }
    
    void preinit();
    
    void init();
    
    void tick(float dt);
    
    void finish();
    
private:
    entt::registry& reg;
    std::vector<std::unique_ptr<System>> systems;
    
};

#endif
