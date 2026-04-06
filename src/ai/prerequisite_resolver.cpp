#include <ai/prerequisite_resolver.h>
#include <ai/action_registry.h>
#include <ai/actions/action.h>
#include "util/log.h"

namespace dy {

PrerequisiteResolver& PrerequisiteResolver::instance() {
    static PrerequisiteResolver instance;
    return instance;
}

std::unique_ptr<Action> PrerequisiteResolver::resolve(
    ActionID target,
    entt::registry& reg,
    entt::entity entity,
    const ActionParams& params,
    int max_depth
) {
    std::set<int> visited;
    return resolve_impl(target, reg, entity, params, 0, visited);
}

std::unique_ptr<Action> PrerequisiteResolver::resolve_impl(
    ActionID target,
    entt::registry& reg,
    entt::entity entity,
    const ActionParams& params,
    int depth,
    std::set<int>& visited
) {
    int target_id = static_cast<int>(target);

    if (depth > 5 || visited.count(target_id)) {
        return ActionRegistry::instance().create_action(target, reg, entity, params);
    }

    visited.insert(target_id);

    const ActionDescriptor* descriptor = ActionRegistry::instance().get_descriptor(target);
    if (!descriptor) {
        return nullptr;
    }

    // Return the first unsatisfied prerequisite's action (recursively resolved).
    // The AI will re-evaluate after each action completes, so only one step is needed at a time.
    for (const auto& prereq : descriptor->prerequisites) {
        bool satisfied = prereq.is_satisfied(reg, entity);
        dy::log() << "[Build/Prereq] action=" << static_cast<int>(target)
            << " prereq='" << prereq.description << "': "
            << (satisfied ? "satisfied" : "UNSATISFIED") << "\n";
        if (!satisfied) {
            ActionID prereq_id = prereq.resolve_action(reg, entity);
            dy::log() << "[Build/Prereq] Resolving sub-action "
                << static_cast<int>(prereq_id) << " (depth " << depth + 1 << ")\n";
            return resolve_impl(prereq_id, reg, entity, {}, depth + 1, visited);
        }
    }

    return ActionRegistry::instance().create_action(target, reg, entity, params);
}

} // namespace dy
