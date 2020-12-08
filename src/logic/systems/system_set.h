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
    void pre_add() {
        pre_systems.push_back(std::make_unique<T>(reg));
    }

    template<typename T, typename std::enable_if<std::is_base_of<System, T>::value>::type* = nullptr>
    void post_add() {
        post_systems.push_back(std::make_unique<T>(reg));
    }
    
    void preinit();
    
    void init();
    
    void pre_tick(float dt);

    void post_tick(float dt);
    
    void finish();
    
private:
    entt::registry& reg;
    std::vector<std::unique_ptr<System>> pre_systems;
    std::vector<std::unique_ptr<System>> post_systems;
    
};

#endif
