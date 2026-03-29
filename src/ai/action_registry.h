#ifndef ACTION_REGISTRY_H
#define ACTION_REGISTRY_H

#include <unordered_map>
#include <memory>
#include <functional>
#include <entt/entt.hpp>
#include <ai/action_id.h>
#include <ai/action_descriptor.h>

namespace dy {

class Action;

/// Centralized registry for all action types
/// Singleton pattern: access via ActionRegistry::instance()
class ActionRegistry {
public:
    static ActionRegistry& instance();

    /// Register an action descriptor
    /// Called at game startup to populate the registry
    void register_descriptor(const ActionDescriptor& desc);

    /// Initialize all action descriptors (called at game startup)
    void initialize_descriptors();

    /// Get descriptor by ActionID
    /// Returns nullptr if not found
    const ActionDescriptor* get_descriptor(ActionID id) const;

    /// Lookup ActionID by name
    /// Returns a default-constructed ActionID if not found
    ActionID lookup_id(const std::string& name) const;

    /// Create an action instance by ActionID
    /// Parameters:
    ///   - ActionID id: the action type to create
    ///   - entt::registry& reg: game registry for component access
    ///   - entt::entity entity: the entity performing the action
    ///   - const ActionParams& params: parameterized action data
    /// Returns: unique_ptr to the created Action, or nullptr if ActionID not found
    std::unique_ptr<Action> create_action(
        ActionID id,
        entt::registry& reg,
        entt::entity entity,
        const ActionParams& params = {}
    ) const;

    /// Get the number of registered actions
    size_t descriptor_count() const { return descriptors.size(); }

private:
    ActionRegistry() = default;

    // Prevent copying
    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;

    std::unordered_map<int, ActionDescriptor> descriptors;  // key = static_cast<int>(ActionID)
    std::unordered_map<std::string, ActionID> name_to_id;
};

} // namespace dy

#endif
