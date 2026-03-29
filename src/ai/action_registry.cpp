#include <ai/action_registry.h>
#include <ai/actions/action.h>

namespace dy {

ActionRegistry& ActionRegistry::instance() {
    static ActionRegistry instance;
    return instance;
}

void ActionRegistry::register_descriptor(const ActionDescriptor& desc) {
    int id = static_cast<int>(desc.id);
    descriptors[id] = desc;
    name_to_id[desc.name] = desc.id;
}

const ActionDescriptor* ActionRegistry::get_descriptor(ActionID id) const {
    int key = static_cast<int>(id);
    auto it = descriptors.find(key);
    if (it != descriptors.end()) {
        return &it->second;
    }
    return nullptr;
}

ActionID ActionRegistry::lookup_id(const std::string& name) const {
    auto it = name_to_id.find(name);
    if (it != name_to_id.end()) {
        return it->second;
    }
    return ActionID::Wander;  // Default fallback
}

std::unique_ptr<Action> ActionRegistry::create_action(
    ActionID id,
    entt::registry& reg,
    entt::entity entity,
    const ActionParams& params
) const {
    const ActionDescriptor* desc = get_descriptor(id);
    if (!desc) {
        return nullptr;
    }
    return desc->create(reg, entity, params);
}

} // namespace dy
