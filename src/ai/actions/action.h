#ifndef ACTION_H
#define ACTION_H

#include <memory>
#include <string>
#include <entt/entt.hpp>

#include <logic/components/position.h>

namespace dy {

class Action {
public:
    Action(entt::registry& reg, entt::entity entity) : reg(reg), entity(entity) {}

    virtual std::unique_ptr<Action> act(std::unique_ptr<Action> self, double dt) = 0;

    /// Human-readable description of the current stage/state. Used for speech bubbles and debug UI.
    virtual std::string describe() const { return ""; }

    virtual ~Action() {}

    bool interruptible = false;

    /// Set when the action fails. Used by AIMemory / LLM feedback in Phase 2+.
    std::string failure_reason;

    entt::registry& reg;
    entt::entity entity;
};

};

#endif
