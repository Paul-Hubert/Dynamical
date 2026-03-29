# Phase 1: Action System Foundation (No LLM)

**Goal**: Establish parameterized action system with registry-based architecture, enabling any action to be created dynamically with typed parameters.

**Scope**: No LLM involved — this phase modernizes the existing action system to support future LLM integration.

**Estimated Scope**: ~1000 lines of new code, ~500 lines modified

**Completion Criteria**: Game runs identically to current version, but all actions use new `ActionBase` system and accept parameters.

---

## Context: Why This Phase Matters

The current system hardcodes action creation in each behavior's `deploy()` method and manual prerequisite chaining. This makes it impossible for an LLM (or UI) to dynamically request an action with specific parameters.

**Phase 1 goal**: Build a universal action factory that allows:
- Creating any action from an enum + parameters dictionary
- Automatically resolving prerequisites (e.g., "to eat I need food" → auto-chain harvest)
- Enabling future LLM requests like `{"action": "Harvest", "target": "oak_tree"}`

This phase touches **no game logic** — existing actions (Wander, Eat, Harvest) continue to work identically.

---

## 1. Core Structures

### 1.1 ActionID Enum

Create `src/ai/actions/action_id.h`:

```cpp
#pragma once

enum class ActionID : int {
    None = 0,
    Wander,
    Eat,
    Harvest,
    Mine,
    Hunt,
    Build,
    Sleep,
    Trade,
    Talk,
    Craft,
    Fish,
    Explore,
    Flee
};

// Helper
inline const char* action_id_name(ActionID id) {
    switch (id) {
        case ActionID::None:    return "None";
        case ActionID::Wander:  return "Wander";
        case ActionID::Eat:     return "Eat";
        case ActionID::Harvest: return "Harvest";
        // ... all others
        default:                return "Unknown";
    }
}
```

### 1.2 Action Parameters

Create `src/ai/actions/action_params.h`:

```cpp
#pragma once
#include <string>
#include <entt/entt.hpp>

struct TradeOffer {
    std::string give_item;       // e.g. "berry"
    int give_amount = 1;         // e.g. 5
    std::string want_item;       // e.g. "wood"
    int want_amount = 1;         // e.g. 2
};

struct ActionParams {
    // Target specification
    std::string target_type;                    // "oak_tree", "stone_deposit", "berry_bush", etc.
    entt::entity target_entity = entt::null;    // Resolved from target_type or target_name

    // Entity interaction
    std::string target_name;                    // "Entity#42" for Talk/Trade
    std::string message;                        // Text for Talk action
    TradeOffer trade_offer;                     // For Trade action

    // Resource specification
    std::string resource;                       // "wood", "stone", "berry"
    int amount = 1;

    // Crafting
    std::string recipe;                         // "wooden_sword", "stone_pickaxe"

    // Building
    std::string structure;                      // "house", "wall", "storage"

    // Generic
    std::string direction;                      // "north", "south", etc.
    float duration = 0;                         // Seconds for Sleep
};
```

### 1.3 Prerequisite Structures

Create `src/ai/actions/action_descriptor.h`:

```cpp
#pragma once
#include "action_id.h"
#include "action_params.h"
#include <functional>
#include <vector>
#include <memory>

class Action;

struct ActionPrerequisite {
    // Check if this prerequisite is satisfied
    std::function<bool(entt::registry&, entt::entity)> is_satisfied;

    // If not satisfied, which action would satisfy it?
    std::function<ActionID(entt::registry&, entt::entity)> resolve_action;

    // Human-readable description
    std::string description;  // e.g., "needs food in inventory"
};

struct ActionDescriptor {
    ActionID id;
    std::string name;           // "Eat" -- matches LLM output
    std::string description;    // "Consume food to reduce hunger"
    std::vector<ActionPrerequisite> prerequisites;

    // Factory function to create action instances
    std::function<std::unique_ptr<Action>(entt::registry&, entt::entity, const ActionParams&)> create;
};
```

---

## 2. Action Registry

Create `src/ai/actions/action_registry.h` and `action_registry.cpp`:

```cpp
// action_registry.h
#pragma once
#include "action_descriptor.h"
#include <unordered_map>
#include <vector>

class ActionRegistry {
public:
    static ActionRegistry& instance();

    void register_action(ActionDescriptor descriptor);
    const ActionDescriptor* get(ActionID id) const;
    std::vector<const ActionDescriptor*> get_all() const;

    // Filter actions available to this entity (e.g., only Eat if has BasicNeeds)
    std::vector<const ActionDescriptor*> get_available(entt::registry& reg, entt::entity entity) const;

private:
    ActionRegistry() = default;
    std::unordered_map<int, ActionDescriptor> registry;
};
```

```cpp
// action_registry.cpp
#include "action_registry.h"

ActionRegistry& ActionRegistry::instance() {
    static ActionRegistry reg;
    return reg;
}

void ActionRegistry::register_action(ActionDescriptor descriptor) {
    registry[static_cast<int>(descriptor.id)] = descriptor;
}

const ActionDescriptor* ActionRegistry::get(ActionID id) const {
    auto it = registry.find(static_cast<int>(id));
    if (it != registry.end()) return &it->second;
    return nullptr;
}

std::vector<const ActionDescriptor*> ActionRegistry::get_all() const {
    std::vector<const ActionDescriptor*> result;
    for (const auto& [id, desc] : registry) {
        result.push_back(&desc);
    }
    return result;
}

std::vector<const ActionDescriptor*> ActionRegistry::get_available(entt::registry& reg, entt::entity entity) const {
    std::vector<const ActionDescriptor*> available;
    for (const auto& [id, desc] : registry) {
        // Simple check: entity must be valid
        // Can add component checks later
        if (reg.valid(entity)) {
            available.push_back(&desc);
        }
    }
    return available;
}
```

---

## 3. CRTP Action Factory

Create `src/ai/actions/action_factory.h`:

```cpp
#pragma once
#include "action.h"
#include "action_registry.h"

// CRTP base class for all actions
template<typename Derived, ActionID ID>
class ActionBase : public Action {
public:
    using Action::Action;

    ActionID id() const final { return ID; }

    // Each derived class implements these static methods
    static const char* action_name();                                  // "Eat"
    static const char* action_description();                          // "Consume food..."
    static std::vector<ActionPrerequisite> get_prerequisites();       // []

    // Auto-registration via static initializer
    static inline bool registered = register_action();

private:
    static bool register_action() {
        ActionRegistry::instance().register_action(ActionDescriptor{
            .id = ID,
            .name = Derived::action_name(),
            .description = Derived::action_description(),
            .prerequisites = Derived::get_prerequisites(),
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<Derived>(r, e, p);
            }
        });
        return true;
    }
};
```

---

## 4. Modified Action Base Class

Modify `src/ai/actions/action.h`:

```cpp
#pragma once
#include <memory>
#include <entt/entt.hpp>
#include "action_id.h"
#include "action_params.h"

class Action {
public:
    Action(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : reg(reg), entity(entity), params(params) {}

    virtual ~Action() = default;

    virtual ActionID id() const = 0;
    virtual std::string describe() const = 0;  // Current state for speech bubble
    virtual std::unique_ptr<Action> act(std::unique_ptr<Action> self) = 0;

    // Prerequisite linking (unchanged from current system)
    void link(Action* before, std::unique_ptr<Action> after);
    std::unique_ptr<Action> nextAction();

    bool interruptible = false;

protected:
    entt::registry& reg;
    entt::entity entity;
    ActionParams params;

private:
    std::unique_ptr<Action> next = nullptr;
};
```

---

## 5. Prerequisite Resolver

Create `src/ai/actions/prerequisite_resolver.h` and `.cpp`:

```cpp
// prerequisite_resolver.h
#pragma once
#include "action_id.h"
#include <memory>
#include <vector>
#include <unordered_set>

class Action;

struct PrerequisiteResolution {
    bool satisfied = false;
    std::unique_ptr<Action> action_chain = nullptr;  // Head of prerequisite chain
};

class PrerequisiteResolver {
public:
    // Recursively resolve prerequisites for an action
    // Returns the head of a linked action chain (may include prerequisite actions)
    PrerequisiteResolution resolve(
        entt::registry& reg,
        entt::entity entity,
        ActionID target_action,
        const ActionParams& params
    );

private:
    PrerequisiteResolution resolve_recursive(
        entt::registry& reg,
        entt::entity entity,
        ActionID target_action,
        const ActionParams& params,
        std::unordered_set<int>& visited,
        int depth
    );

    static constexpr int MAX_DEPTH = 5;  // Prevent infinite cycles
};
```

```cpp
// prerequisite_resolver.cpp
#include "prerequisite_resolver.h"
#include "action_registry.h"
#include "action_factory.h"

PrerequisiteResolution PrerequisiteResolver::resolve(
    entt::registry& reg,
    entt::entity entity,
    ActionID target_action,
    const ActionParams& params
) {
    std::unordered_set<int> visited;
    return resolve_recursive(reg, entity, target_action, params, visited, 0);
}

PrerequisiteResolution PrerequisiteResolver::resolve_recursive(
    entt::registry& reg,
    entt::entity entity,
    ActionID target_action,
    const ActionParams& params,
    std::unordered_set<int>& visited,
    int depth
) {
    if (depth > MAX_DEPTH) {
        return PrerequisiteResolution{false, nullptr};
    }

    int action_int = static_cast<int>(target_action);
    if (visited.count(action_int)) {
        return PrerequisiteResolution{false, nullptr};  // Cycle detected
    }
    visited.insert(action_int);

    const auto* descriptor = ActionRegistry::instance().get(target_action);
    if (!descriptor) {
        return PrerequisiteResolution{false, nullptr};
    }

    // Check all prerequisites
    std::unique_ptr<Action> prereq_chain = nullptr;
    for (const auto& prereq : descriptor->prerequisites) {
        if (!prereq.is_satisfied(reg, entity)) {
            // Need to resolve this prerequisite
            ActionID prereq_action = prereq.resolve_action(reg, entity);
            auto prereq_res = resolve_recursive(reg, entity, prereq_action, params, visited, depth + 1);

            if (!prereq_res.satisfied) {
                return PrerequisiteResolution{false, nullptr};
            }

            if (!prereq_chain) {
                prereq_chain = std::move(prereq_res.action_chain);
            } else {
                // Link to existing chain
                if (prereq_chain) {
                    auto tail = prereq_chain.get();
                    while (tail->nextAction()) {
                        tail = tail->nextAction().get();
                    }
                    tail->link(tail, std::move(prereq_res.action_chain));
                }
            }
        }
    }

    // Create the target action
    auto target = descriptor->create(reg, entity, params);

    if (prereq_chain) {
        // Link target as successor to prerequisite chain
        auto tail = prereq_chain.get();
        while (tail->nextAction()) {
            tail = tail->nextAction().get();
        }
        tail->link(tail, std::move(target));
        return PrerequisiteResolution{true, std::move(prereq_chain)};
    }

    return PrerequisiteResolution{true, std::move(target)};
}
```

---

## 6. Migrate Existing Actions

### 6.1 WanderAction

Modify `src/ai/actions/wander_action.h`:

```cpp
#pragma once
#include "action_factory.h"

class WanderAction : public ActionBase<WanderAction, ActionID::Wander> {
public:
    WanderAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    static const char* action_name() { return "Wander"; }
    static const char* action_description() { return "Move aimlessly around the world"; }
    static std::vector<ActionPrerequisite> get_prerequisites() { return {}; }

    std::string describe() const override;
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;
};
```

Update `src/ai/actions/wander_action.cpp`:

```cpp
#include "wander_action.h"

WanderAction::WanderAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params) {}

std::string WanderAction::describe() const {
    return "Wandering...";
}

std::unique_ptr<Action> WanderAction::act(std::unique_ptr<Action> self) {
    // ... existing logic unchanged
    return self;
}
```

### 6.2 EatAction

Modify `src/ai/actions/eat_action.h`:

```cpp
#pragma once
#include "action_factory.h"

class EatAction : public ActionBase<EatAction, ActionID::Eat> {
public:
    EatAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    static const char* action_name() { return "Eat"; }
    static const char* action_description() { return "Consume food to reduce hunger"; }
    static std::vector<ActionPrerequisite> get_prerequisites();

    std::string describe() const override;
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;
};
```

Update `src/ai/actions/eat_action.cpp`:

```cpp
#include "eat_action.h"

EatAction::EatAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params) {}

std::vector<ActionPrerequisite> EatAction::get_prerequisites() {
    return {
        ActionPrerequisite{
            .is_satisfied = [](entt::registry& reg, entt::entity entity) {
                // Check if entity has food (simplified)
                return true;  // For now, assume food is available
            },
            .resolve_action = [](entt::registry& reg, entt::entity entity) {
                return ActionID::Harvest;  // Need to harvest food
            },
            .description = "needs food"
        }
    };
}

std::string EatAction::describe() const {
    return "Eating...";
}

std::unique_ptr<Action> EatAction::act(std::unique_ptr<Action> self) {
    // ... existing logic unchanged
    return self;
}
```

### 6.3 HarvestAction

Similarly update `src/ai/actions/harvest_action.h` and `.cpp` to use `ActionBase`.

---

## 7. Action Skeletons

Create empty skeleton files for all new actions (these will be implemented in Phase 6):

- `src/ai/actions/mine_action.h` / `.cpp`
- `src/ai/actions/hunt_action.h` / `.cpp`
- `src/ai/actions/build_action.h` / `.cpp`
- `src/ai/actions/sleep_action.h` / `.cpp`
- `src/ai/actions/trade_action.h` / `.cpp`
- `src/ai/actions/talk_action.h` / `.cpp`
- `src/ai/actions/craft_action.h` / `.cpp`
- `src/ai/actions/fish_action.h` / `.cpp`
- `src/ai/actions/explore_action.h` / `.cpp`
- `src/ai/actions/flee_action.h` / `.cpp`

Each skeleton should:

```cpp
// mine_action.h
#pragma once
#include "action_factory.h"

class MineAction : public ActionBase<MineAction, ActionID::Mine> {
public:
    MineAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    static const char* action_name() { return "Mine"; }
    static const char* action_description() { return "Extract minerals from deposits"; }
    static std::vector<ActionPrerequisite> get_prerequisites() { return {}; }

    std::string describe() const override;
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;
};
```

---

## 8. CMakeLists.txt Updates

Add new headers/sources to the build:

```cmake
target_sources(dynamical PRIVATE
    # ... existing files
    src/ai/actions/action_id.h
    src/ai/actions/action_params.h
    src/ai/actions/action_descriptor.h
    src/ai/actions/action_registry.h
    src/ai/actions/action_registry.cpp
    src/ai/actions/action_factory.h
    src/ai/actions/prerequisite_resolver.h
    src/ai/actions/prerequisite_resolver.cpp

    # Modified
    src/ai/actions/action.h
    src/ai/actions/wander_action.h
    src/ai/actions/wander_action.cpp
    src/ai/actions/eat_action.h
    src/ai/actions/eat_action.cpp
    src/ai/actions/harvest_action.h
    src/ai/actions/harvest_action.cpp

    # New skeletons
    src/ai/actions/mine_action.h
    src/ai/actions/mine_action.cpp
    # ... etc for all new action types
)
```

---

## 9. Testing Checklist

- [ ] Code compiles without errors
- [ ] ActionRegistry populates with all registered actions
- [ ] `ActionRegistry::get_available()` returns valid actions
- [ ] Existing behavior (WanderBehavior, EatBehavior) still works
- [ ] Wander/Eat/Harvest actions work identically to current version
- [ ] AISys::tick() runs without LLM modifications
- [ ] Prerequisites can be manually triggered (test: force prereq check)
- [ ] Action parameters are accepted (but not used yet)
- [ ] Game loop runs at normal speed

---

## 10. Next Phase

**Phase 2** introduces Personality & Memory components. These will be consumed by the LLM system in Phase 3 but are orthogonal to the action system.

Phase 1 + Phase 2 can be developed in parallel if desired.

---

**Reference Sections from Global Doc:**
- Section 5: Action System Redesign
- Section 5.4: CRTP Action Factory
- Section 5.5: Modified Action Base Class
- Section 6: Action Parameters
- Section 16: File Structure
