# Build Action

## Overview

Construct a structure in the world (house, wall, storage). The entity validates materials, finds a valid build site, pathfinds to it, consumes resources, and waits for `BuildSys` to spawn the structure entity. This is the most complex action — it creates persistent world objects.

## ActionID

`ActionID::Build`

## Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `structure` | `std::string` | `""` | Structure type: `"house"`, `"wall"`, `"storage"` |

## Infrastructure Changes

### Object Additions

**File**: `src/logic/components/object.h`

```cpp
enum Type {
    plant,
    being,
    resource,   // from Mine action
    building    // NEW: player-constructed structures
};

enum Identifier {
    // ... existing ...
    structure,  // NEW: generic structure identifier
};
```

### New Component: `Building`

**File**: `src/logic/components/building.h`

Active construction state — attached to the builder entity while building:

```cpp
#ifndef BUILDING_H
#define BUILDING_H

#include <entt/entt.hpp>
#include <string>
#include <glm/glm.hpp>
#include <logic/components/time.h>

namespace dy {

class Building {
public:
    Building(entt::registry& reg, const std::string& structure_type, glm::vec2 build_pos)
        : structure_type(structure_type), build_pos(build_pos) {
        start = reg.ctx<dy::Time>().current;
    }
    std::string structure_type;
    glm::vec2 build_pos;
    time_t start;
};

}

#endif
```

### New Component: `Structure`

**File**: `src/logic/components/structure.h`

Permanent tag on completed structures in the world:

```cpp
#ifndef STRUCTURE_H
#define STRUCTURE_H

#include <string>

namespace dy {

class Structure {
public:
    Structure(const std::string& type) : type(type) {}
    std::string type;  // "house", "wall", "storage"
};

}

#endif
```

### New System: `BuildSys`

**File**: `src/logic/systems/build.cpp`

**Registration**:
- Add `DEFINE_SYSTEM(BuildSys)` to `src/logic/systems/system_list.h`
- Add `set->pre_add<BuildSys>()` to `src/logic/game.cpp` after `CraftSys`

```cpp
#include "system_list.h"

#include <logic/components/building.h>
#include <logic/components/structure.h>
#include <logic/components/action_bar.h>
#include <logic/components/object.h>
#include <logic/components/renderable.h>
#include <logic/map/map_manager.h>
#include <logic/factories/factory_list.h>

using namespace dy;

void BuildSys::preinit() {}
void BuildSys::init() {}

void BuildSys::tick(double dt) {
    OPTICK_EVENT();

    constexpr time_t duration = 30 * Time::minute;

    auto& time = reg.ctx<Time>();

    auto view = reg.view<Building>();
    view.each([&](const auto entity, auto& building) {
        if (!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, building.start, building.start + duration);
        }

        if (time.current > building.start + duration) {
            // Spawn the structure entity in the world
            auto& map = reg.ctx<MapManager>();
            auto* tile = map.getTile(building.build_pos);

            if (tile && tile->object == entt::null) {
                // Determine color by structure type
                Color color(0.6, 0.4, 0.2, 1.0);  // default brown
                if (building.structure_type == "house") color = Color(0.7, 0.5, 0.3, 1.0);
                else if (building.structure_type == "wall") color = Color(0.5, 0.5, 0.5, 1.0);
                else if (building.structure_type == "storage") color = Color(0.4, 0.3, 0.2, 1.0);

                auto structure_entity = dy::buildObject(reg, building.build_pos,
                    Object::building, Object::structure, glm::vec2(1, 1), color);
                reg.emplace<Structure>(structure_entity, building.structure_type);
            }

            reg.remove<Building>(entity);
            reg.remove<ActionBar>(entity);
        }
    });
}

void BuildSys::finish() {}
```

### Build Cost Table

Hardcoded material requirements (could be data-driven later):

| Structure | Materials |
|-----------|-----------|
| `"wall"` | 5 wood |
| `"house"` | 10 wood + 5 stone |
| `"storage"` | 5 wood + 3 stone |

```cpp
// Helper function in build_action.cpp
static bool get_build_cost(const std::string& structure,
                           std::vector<std::pair<Item::ID, int>>& cost) {
    if (structure == "wall") {
        cost = {{Item::wood, 5}};
    } else if (structure == "house") {
        cost = {{Item::wood, 10}, {Item::stone, 5}};
    } else if (structure == "storage") {
        cost = {{Item::wood, 5}, {Item::stone, 3}};
    } else {
        return false;  // unknown structure
    }
    return true;
}
```

## Stages

```
machine_
    .stage("checking materials", [this](double) -> StageStatus {
        if (!get_build_cost(this->params.structure, build_cost_)) {
            this->failure_reason = "unknown structure: " + this->params.structure;
            return StageStatus::Failed;
        }

        auto& storage = this->reg.get<Storage>(this->entity);
        for (auto& [item_id, amount] : build_cost_) {
            if (storage.amount(item_id) < amount) {
                this->failure_reason = "not enough " + std::string(Item::to_string(item_id))
                    + " (need " + std::to_string(amount) + ")";
                return StageStatus::Failed;
            }
        }
        return StageStatus::Complete;
    })
    .stage("choosing build site", [this](double) -> StageStatus {
        auto& map = this->reg.ctx<MapManager>();
        auto my_pos = this->reg.get<Position>(this->entity);

        // Find empty grass tile within 10 tiles
        bool found = false;
        auto path = map.pathfind(my_pos, [&](glm::vec2 pos) {
            auto* tile = map.getTile(pos);
            if (tile && tile->terrain == Tile::grass && tile->object == entt::null) {
                build_pos_ = pos;
                found = true;
                return true;
            }
            return false;
        }, 10);  // limit search radius

        if (!found || path.tiles.empty()) {
            this->failure_reason = "no valid build site nearby";
            return StageStatus::Failed;
        }

        this->reg.emplace<Path>(this->entity, std::move(path));
        return StageStatus::Complete;
    })
    .stage("walking to site",
        stage_primitives::wait_until_removed<Path>(reg, entity))
    .stage("consuming materials", [this](double) -> StageStatus {
        auto& storage = this->reg.get<Storage>(this->entity);
        for (auto& [item_id, amount] : build_cost_) {
            storage.remove(Item(item_id, amount));
        }
        this->reg.emplace<Building>(this->entity, this->reg, this->params.structure, build_pos_);
        return StageStatus::Complete;
    })
    .stage("building",
        stage_primitives::wait_until_removed<Building>(reg, entity));
```

## Action Header

```cpp
#ifndef BUILD_ACTION_H
#define BUILD_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <logic/components/item.h>
#include <glm/glm.hpp>
#include <vector>
#include <utility>

namespace dy {

class BuildAction : public ActionBase<BuildAction> {
public:
    BuildAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    std::vector<std::pair<Item::ID, int>> build_cost_;
    glm::vec2 build_pos_{0, 0};
    ActionStageMachine machine_;
};

}

#endif
```

## Action Implementation

```cpp
std::unique_ptr<Action> BuildAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();
    StageStatus status = machine_.tick(dt);
    if (status == StageStatus::Failed) return nullptr;
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string BuildAction::describe() const {
    return "Build: " + machine_.current_stage_name();
}
```

## Prerequisites

Has required materials:

```cpp
// In action_registry_init.cpp:
{
    .description = "needs building materials",
    .is_satisfied = [](auto& r, auto e) {
        if (!r.template all_of<Storage>(e)) return false;
        auto& s = r.template get<Storage>(e);
        // At minimum, need some wood
        return s.amount(Item::wood) >= 5;
    },
    .resolve_action = [](auto& r, auto e) { return ActionID::Harvest; }
    // Note: Harvest currently only yields berries.
    // To get wood, HarvestAction needs to be extended to harvest trees,
    // or a separate "chop wood" action is needed.
    // Alternative: resolve to Mine for stone.
}
```

## Failure Conditions

- `"unknown structure: [name]"` — structure type not in build cost table
- `"not enough [item] (need [amount])"` — insufficient materials
- `"no valid build site nearby"` — no empty grass tiles within 10 tiles

## Memory Events

- Type: `"action_completed"`
- Description: `"Built a [structure_type]"`

## Example LLM JSON

```json
{
  "action": "Build",
  "structure": "house",
  "thought": "I have enough wood and stone. Time to build a shelter.",
  "speech": "Let me build a house here."
}
```

```json
{
  "action": "Build",
  "structure": "wall",
  "thought": "A wall would protect the area.",
  "speech": "Building a wall."
}
```

```json
{
  "action": "Build",
  "structure": "storage",
  "thought": "I need somewhere to keep my items safe.",
  "speech": "Time to make a storage chest."
}
```

## Notes

- `BuildSys` uses `dy::buildObject()` which calls `MapManager::insert()` — the structure is properly registered in the spatial system
- Structures are permanent world entities with `Object(building, structure)` + `Structure{type}` components
- Future: structures could provide bonuses (house = sleep quality, storage = extra inventory, wall = territory)
- The `wood` item needs a source — extend `HarvestAction` to yield wood from `Object::tree` targets, or add a dedicated "Chop" action
