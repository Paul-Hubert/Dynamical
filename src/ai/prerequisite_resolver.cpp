#include <ai/prerequisite_resolver.h>
#include <ai/action_registry.h>
#include <ai/actions/action.h>

namespace dy {

PrerequisiteResolver& PrerequisiteResolver::instance() {
    static PrerequisiteResolver instance;
    return instance;
}

std::vector<std::unique_ptr<Action>> PrerequisiteResolver::resolve(
    ActionID target,
    entt::registry& reg,
    entt::entity entity,
    const ActionParams& params,
    int max_depth
) {
    std::set<int> visited;
    return resolve_impl(target, reg, entity, params, 0, visited);
}

std::vector<std::unique_ptr<Action>> PrerequisiteResolver::resolve_impl(
    ActionID target,
    entt::registry& reg,
    entt::entity entity,
    const ActionParams& params,
    int depth,
    std::set<int>& visited
) {
    int target_id = static_cast<int>(target);

    // Cycle or depth guard — just return the target action directly
    if (depth > 5 || visited.count(target_id)) {
        std::vector<std::unique_ptr<Action>> chain;
        chain.push_back(ActionRegistry::instance().create_action(target, reg, entity, params));
        return chain;
    }

    visited.insert(target_id);

    const ActionDescriptor* descriptor = ActionRegistry::instance().get_descriptor(target);
    if (!descriptor) {
        return {};
    }

    // Build chain: all unsatisfied prerequisite actions first, then the target action last.
    std::vector<std::unique_ptr<Action>> chain;

    for (const auto& prereq : descriptor->prerequisites) {
        if (!prereq.is_satisfied(reg, entity, params)) {
            ActionID prereq_id = prereq.resolve_action(reg, entity, params);
            auto sub_chain = resolve_impl(prereq_id, reg, entity, {}, depth + 1, visited);
            for (auto& a : sub_chain) chain.push_back(std::move(a));
        }
    }

    // Always append the target action — this is what was previously lost
    chain.push_back(ActionRegistry::instance().create_action(target, reg, entity, params));
    return chain;
}

} // namespace dy
