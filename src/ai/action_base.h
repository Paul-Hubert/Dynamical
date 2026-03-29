#ifndef ACTION_BASE_H
#define ACTION_BASE_H

#include <memory>
#include <entt/entt.hpp>
#include <ai/actions/action.h>
#include <ai/action_id.h>

namespace dy {

/// Base template for all concrete action types using CRTP (Curiously Recurring Template Pattern)
/// Provides automatic dispatch from act() to derived class's act_impl()
///
/// Usage:
///   class WanderAction : public ActionBase<WanderAction> {
///   public:
///       WanderAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
///           : ActionBase(reg, entity, params) {}
///
///   protected:
///       std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self) override {
///           // Implementation here
///           return self;
///       }
///   };
template<typename Derived>
class ActionBase : public Action {
public:
    ActionBase(entt::registry& reg, entt::entity entity, const ActionParams& params)
        : Action(reg, entity), params(params) {}

    virtual ~ActionBase() = default;

    /// Final implementation of act() that dispatches to derived class's act_impl()
    virtual std::unique_ptr<Action> act(std::unique_ptr<Action> self, double dt) override {
        Derived* derived = static_cast<Derived*>(this);
        return derived->act_impl(std::move(self), dt);
    }

    /// Default deploy() does nothing (can be overridden by derived classes if needed)
    virtual std::unique_ptr<Action> deploy() {
        return nullptr;
    }

public:
    /// Subclass-defined action implementation
    /// Should return self to continue execution, or nullptr to complete
    /// Note: marked public for CRTP dispatch; subclasses should treat as protected interface
    virtual std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) = 0;

protected:

    /// Action parameters (target, message, resource, etc.)
    ActionParams params;
};

} // namespace dy

#endif
