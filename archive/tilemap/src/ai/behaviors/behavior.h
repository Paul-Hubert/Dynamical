#ifndef BEHAVIOR_H
#define BEHAVIOR_H

#include <entt/entt.hpp>

#include <ai/actions/action.h>

namespace dy {

class Behavior {
public:
    Behavior(entt::registry& reg, entt::entity entity) : reg(reg), entity(entity) {}
    virtual std::unique_ptr<Action> action() = 0;
    virtual ~Behavior() {}
    
    entt::registry& reg;
    entt::entity entity;
    
};

}

#endif
