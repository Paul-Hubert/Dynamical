# Craft Action

## Overview

Create items from resources using a recipe. The entity checks the recipe registry, validates it has required ingredients, consumes them, and waits for `CraftSys` to produce the output. This action introduces the entire crafting recipe system.

## ActionID

`ActionID::Craft`

## Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `recipe` | `std::string` | `""` | Recipe name: `"plank"`, `"wooden_sword"`, `"stone_pickaxe"`, `"cooked_meat"`, `"cooked_fish"` |

## Infrastructure Changes

### Item::ID Additions

**File**: `src/logic/components/item.h`

All items needed for crafting:

```cpp
enum ID : int {
    null,
    berry,
    wood,           // NEW: harvested from trees (update HarvestAction to yield wood from trees)
    stone,          // from Mine
    ore,            // from Mine
    fish,           // from Fish
    meat,           // from Hunt
    plank,          // NEW: crafted from wood
    wooden_sword,   // NEW: crafted from wood
    stone_pickaxe,  // NEW: crafted from stone + wood
    cooked_meat,    // NEW: crafted from meat + wood
    cooked_fish,    // NEW: crafted from fish + wood
};
```

Also add string conversion helpers:

```cpp
static ID from_string(const std::string& name) {
    if (name == "berry") return berry;
    if (name == "wood") return wood;
    if (name == "stone") return stone;
    if (name == "ore") return ore;
    if (name == "fish") return fish;
    if (name == "meat") return meat;
    if (name == "plank") return plank;
    if (name == "wooden_sword") return wooden_sword;
    if (name == "stone_pickaxe") return stone_pickaxe;
    if (name == "cooked_meat") return cooked_meat;
    if (name == "cooked_fish") return cooked_fish;
    return null;
}

static const char* to_string(ID id) {
    switch (id) {
        case berry: return "berry";
        case wood: return "wood";
        case stone: return "stone";
        case ore: return "ore";
        case fish: return "fish";
        case meat: return "meat";
        case plank: return "plank";
        case wooden_sword: return "wooden_sword";
        case stone_pickaxe: return "stone_pickaxe";
        case cooked_meat: return "cooked_meat";
        case cooked_fish: return "cooked_fish";
        default: return "null";
    }
}
```

### New: Crafting Recipe Registry

**File**: `src/ai/crafting/recipe_registry.h`

```cpp
#ifndef RECIPE_REGISTRY_H
#define RECIPE_REGISTRY_H

#include <string>
#include <vector>
#include <utility>
#include <logic/components/item.h>

namespace dy {

struct CraftingRecipe {
    std::string name;
    std::vector<std::pair<Item::ID, int>> inputs;
    Item::ID output;
    int output_amount;
    float craft_time;  // game-time seconds
};

class RecipeRegistry {
public:
    static RecipeRegistry& instance() {
        static RecipeRegistry registry;
        return registry;
    }

    void initialize();
    const CraftingRecipe* find(const std::string& name) const;
    const std::vector<CraftingRecipe>& all() const { return recipes_; }

private:
    RecipeRegistry() = default;
    std::vector<CraftingRecipe> recipes_;
};

}

#endif
```

**File**: `src/ai/crafting/recipe_registry.cpp`

```cpp
#include "recipe_registry.h"
#include <logic/components/time.h>

using namespace dy;

void RecipeRegistry::initialize() {
    recipes_ = {
        {
            "plank",
            {{Item::wood, 1}},
            Item::plank, 2,
            5.0f * Time::minute
        },
        {
            "wooden_sword",
            {{Item::wood, 3}},
            Item::wooden_sword, 1,
            10.0f * Time::minute
        },
        {
            "stone_pickaxe",
            {{Item::stone, 2}, {Item::wood, 1}},
            Item::stone_pickaxe, 1,
            10.0f * Time::minute
        },
        {
            "cooked_meat",
            {{Item::meat, 1}, {Item::wood, 1}},
            Item::cooked_meat, 1,
            5.0f * Time::minute
        },
        {
            "cooked_fish",
            {{Item::fish, 1}, {Item::wood, 1}},
            Item::cooked_fish, 1,
            5.0f * Time::minute
        },
    };
}

const CraftingRecipe* RecipeRegistry::find(const std::string& name) const {
    for (auto& r : recipes_) {
        if (r.name == name) return &r;
    }
    return nullptr;
}
```

**Initialization**: Call `RecipeRegistry::instance().initialize()` in `Game::start()` after `ActionRegistry::instance().initialize_descriptors()`.

### Adding New Recipes

To add a recipe, just add an entry to the `recipes_` vector in `initialize()`. No other code changes needed. The LLM prompt builder should list available recipes so the LLM knows what can be crafted.

### New Component: `Crafting`

**File**: `src/logic/components/crafting.h`

```cpp
#ifndef CRAFTING_H
#define CRAFTING_H

#include <entt/entt.hpp>
#include <string>
#include <logic/components/time.h>

namespace dy {

class Crafting {
public:
    Crafting(entt::registry& reg, const std::string& recipe_name) : recipe_name(recipe_name) {
        start = reg.ctx<dy::Time>().current;
    }
    std::string recipe_name;
    time_t start;
};

}

#endif
```

### New System: `CraftSys`

**File**: `src/logic/systems/craft.cpp`

**Registration**:
- Add `DEFINE_SYSTEM(CraftSys)` to `src/logic/systems/system_list.h`
- Add `set->pre_add<CraftSys>()` to `src/logic/game.cpp` after `FishSys`

```cpp
#include "system_list.h"

#include <logic/components/crafting.h>
#include <logic/components/storage.h>
#include <logic/components/item.h>
#include <logic/components/action_bar.h>
#include <ai/crafting/recipe_registry.h>

using namespace dy;

void CraftSys::preinit() {}
void CraftSys::init() {}

void CraftSys::tick(double dt) {
    OPTICK_EVENT();

    auto& time = reg.ctx<Time>();

    auto view = reg.view<Crafting>();
    view.each([&](const auto entity, auto& crafting) {
        const CraftingRecipe* recipe = RecipeRegistry::instance().find(crafting.recipe_name);
        if (!recipe) {
            reg.remove<Crafting>(entity);
            if (reg.all_of<ActionBar>(entity)) reg.remove<ActionBar>(entity);
            return;
        }

        time_t duration = static_cast<time_t>(recipe->craft_time);

        if (!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, crafting.start, crafting.start + duration);
        }

        if (time.current > crafting.start + duration) {
            auto& storage = reg.get<Storage>(entity);
            storage.add(Item(recipe->output, recipe->output_amount));

            reg.remove<Crafting>(entity);
            reg.remove<ActionBar>(entity);
        }
    });
}

void CraftSys::finish() {}
```

## Stages

```
machine_
    .stage("checking recipe", [this](double) -> StageStatus {
        const CraftingRecipe* recipe = RecipeRegistry::instance().find(this->params.recipe);
        if (!recipe) {
            this->failure_reason = "unknown recipe: " + this->params.recipe;
            return StageStatus::Failed;
        }

        auto& storage = this->reg.get<Storage>(this->entity);
        for (auto& [item_id, amount] : recipe->inputs) {
            if (storage.amount(item_id) < amount) {
                this->failure_reason = "missing " + std::string(Item::to_string(item_id))
                    + " x" + std::to_string(amount);
                return StageStatus::Failed;
            }
        }
        recipe_ = recipe;
        return StageStatus::Complete;
    })
    .stage("consuming ingredients", [this](double) -> StageStatus {
        auto& storage = this->reg.get<Storage>(this->entity);
        for (auto& [item_id, amount] : recipe_->inputs) {
            storage.remove(Item(item_id, amount));
        }
        this->reg.emplace<Crafting>(this->entity, this->reg, recipe_->name);
        return StageStatus::Complete;
    })
    .stage("crafting",
        stage_primitives::wait_until_removed<Crafting>(reg, entity));
```

## Action Header

```cpp
#ifndef CRAFT_ACTION_H
#define CRAFT_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>

namespace dy {

struct CraftingRecipe;  // forward declare

class CraftAction : public ActionBase<CraftAction> {
public:
    CraftAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    const CraftingRecipe* recipe_ = nullptr;
    ActionStageMachine machine_;
};

}

#endif
```

## Action Implementation

```cpp
std::unique_ptr<Action> CraftAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();
    StageStatus status = machine_.tick(dt);
    if (status == StageStatus::Failed) return nullptr;
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string CraftAction::describe() const {
    return "Craft: " + machine_.current_stage_name();
}
```

## Prerequisites

Can be enhanced to auto-resolve missing ingredients:

```cpp
// In action_registry_init.cpp:
{
    .description = "needs ingredients",
    .is_satisfied = [](auto& r, auto e) {
        // Could check if entity has any craftable recipe ingredients
        // For simplicity, always satisfied — validation happens in stage 1
        return true;
    },
    .resolve_action = [](auto& r, auto e) { return ActionID::Harvest; }
}
```

## Failure Conditions

- `"unknown recipe: [name]"` — recipe not found in RecipeRegistry
- `"missing [item] x[amount]"` — not enough ingredients in Storage

## Memory Events

- Type: `"action_completed"`
- Description: `"Crafted [recipe_name]"`

## Example LLM JSON

```json
{
  "action": "Craft",
  "recipe": "wooden_sword",
  "thought": "I have enough wood. Time to make a weapon.",
  "speech": "Let me craft a sword."
}
```

```json
{
  "action": "Craft",
  "recipe": "cooked_meat",
  "thought": "Raw meat isn't great. Let me cook it.",
  "speech": "Cooking time!"
}
```
