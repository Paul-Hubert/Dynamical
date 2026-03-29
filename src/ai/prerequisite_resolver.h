#ifndef PREREQUISITE_RESOLVER_H
#define PREREQUISITE_RESOLVER_H

#include <memory>
#include <set>
#include <entt/entt.hpp>
#include <ai/action_id.h>

namespace dy {

class Action;
class ActionRegistry;

/// Resolves action prerequisites and automatically chains dependent actions
///
/// When an action has unsatisfied prerequisites, this resolver recursively
/// builds a chain of prerequisite actions that must execute before the target action.
///
/// Example:
///   Eat action with prerequisite "needs food source"
///   If food is not available, resolver returns: Harvest -> Eat chain
///
/// Features:
///   - Cycle detection via visited set
///   - Depth limiting to prevent pathological chains
///   - Recursive resolution of multi-level dependencies
class PrerequisiteResolver {
public:
    static PrerequisiteResolver& instance();

    /// Main entry point: resolve prerequisites and return head of chain
    /// If no prerequisites needed, returns the target action directly
    ///
    /// Parameters:
    ///   - ActionID target: the action to execute (after prerequisites)
    ///   - entt::registry& reg: game registry for component access
    ///   - entt::entity entity: the entity performing the action
    ///   - const ActionParams& params: action parameters
    ///   - int max_depth: maximum recursion depth to prevent cycles (default: 5)
    ///
    /// Returns: unique_ptr to head of action chain (first prerequisite or target action)
    std::unique_ptr<Action> resolve(
        ActionID target,
        entt::registry& reg,
        entt::entity entity,
        const ActionParams& params = {},
        int max_depth = 5
    );

private:
    PrerequisiteResolver() = default;

    // Prevent copying
    PrerequisiteResolver(const PrerequisiteResolver&) = delete;
    PrerequisiteResolver& operator=(const PrerequisiteResolver&) = delete;

    /// Implementation of resolve with visited set tracking
    std::unique_ptr<Action> resolve_impl(
        ActionID target,
        entt::registry& reg,
        entt::entity entity,
        const ActionParams& params,
        int depth,
        std::set<int>& visited  // Uses int for ActionID comparison
    );
};

} // namespace dy

#endif
