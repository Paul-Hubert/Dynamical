#ifndef ACTION_H
#define ACTION_H

#include <entt/entt.hpp>

#include <logic/components/position.h>

namespace dy {

class Action {
public:
    Action(entt::registry& reg, entt::entity entity) : reg(reg), entity(entity) {}
    virtual std::unique_ptr<Action> act(std::unique_ptr<Action> self) = 0;
    virtual ~Action() {}

    bool interruptible = false;

    entt::registry& reg;
    entt::entity entity;
};

};

#endif
