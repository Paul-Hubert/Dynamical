#ifndef ACTION_H
#define ACTION_H

#include <entt/entt.hpp>

#include <logic/components/position.h>

namespace dy {

class Action {
public:
    Action(entt::registry& reg, entt::entity entity) : reg(reg), entity(entity) {}
    virtual void act(const Position position) = 0;
    virtual ~Action() {}
    
    entt::registry& reg;
    entt::entity entity;
    
    bool interruptible = false;
};

};

#endif
