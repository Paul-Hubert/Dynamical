#ifndef ACTION_DESCRIPTOR_H
#define ACTION_DESCRIPTOR_H

#include <functional>
#include <vector>
#include <memory>
#include <entt/entt.hpp>
#include <ai/action_id.h>

namespace dy {

class Action;

/// Describes a prerequisite that must be satisfied before an action can execute
struct ActionPrerequisite {
    std::string description;        // "needs nearby tree", "needs food source"

    /// Lambda checking if prerequisite is satisfied
    /// Returns true if prerequisite is already met, false otherwise
    std::function<bool(entt::registry&, entt::entity)> is_satisfied;

    /// Lambda returning the ActionID to resolve this prerequisite
    /// Called only if is_satisfied() returns false
    std::function<ActionID(entt::registry&, entt::entity)> resolve_action;
};

/// Describes an action type: how to create it, what prerequisites it has, etc.
struct ActionDescriptor {
    ActionID id;
    std::string name;
    std::vector<ActionPrerequisite> prerequisites;

    /// Factory function to create an instance of this action
    /// Parameters:
    ///   - entt::registry& ref: game registry for component access
    ///   - entt::entity entity: the entity performing the action
    ///   - const ActionParams& params: parameterized action data
    /// Returns: unique_ptr to the created Action
    std::function<std::unique_ptr<Action>(
        entt::registry&,
        entt::entity,
        const ActionParams&
    )> create;
};

} // namespace dy

#endif
