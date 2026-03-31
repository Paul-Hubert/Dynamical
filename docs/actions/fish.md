# Fish Action

## Overview

Catch fish from water. The entity pathfinds to a walkable tile adjacent to water or river, casts a line, and waits for `FishSys` to complete the catch. Yields 1-3 fish.

## ActionID

`ActionID::Fish`

## Parameters

None required. The action automatically finds the nearest water-adjacent tile.

## Infrastructure Changes

### Item::ID Addition

**File**: `src/logic/components/item.h`

```cpp
fish,   // NEW: caught by fishing
```

### New Component: `Fishing`

**File**: `src/logic/components/fishing.h`

```cpp
#ifndef FISHING_H
#define FISHING_H

#include <entt/entt.hpp>
#include <logic/components/time.h>

namespace dy {

class Fishing {
public:
    Fishing(entt::registry& reg) {
        start = reg.ctx<dy::Time>().current;
    }
    time_t start;
};

}

#endif
```

### New System: `FishSys`

**File**: `src/logic/systems/fish.cpp`

**Registration**:
- Add `DEFINE_SYSTEM(FishSys)` to `src/logic/systems/system_list.h`
- Add `set->pre_add<FishSys>()` to `src/logic/game.cpp` after `MineSys`

```cpp
#include "system_list.h"

#include <logic/components/fishing.h>
#include <logic/components/storage.h>
#include <logic/components/item.h>
#include <logic/components/action_bar.h>

using namespace dy;

void FishSys::preinit() {}
void FishSys::init() {}

void FishSys::tick(double dt) {
    OPTICK_EVENT();

    constexpr time_t duration = 10 * Time::minute;

    auto& time = reg.ctx<Time>();

    auto view = reg.view<Fishing>();
    view.each([&](const auto entity, auto& fishing) {
        if (!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, fishing.start, fishing.start + duration);
        }

        if (time.current > fishing.start + duration) {
            auto& storage = reg.get<Storage>(entity);
            int catch_amount = 1 + (std::rand() % 3);  // 1-3 fish
            storage.add(Item(Item::fish, catch_amount));

            reg.remove<Fishing>(entity);
            reg.remove<ActionBar>(entity);
        }
    });
}

void FishSys::finish() {}
```

## Stages

```
machine_
    .stage("finding fishing spot", [this](double) -> StageStatus {
        if (this->reg.all_of<Path>(this->entity))
            this->reg.remove<Path>(this->entity);

        auto& map = this->reg.ctx<MapManager>();
        auto position = this->reg.get<Position>(this->entity);

        bool found = false;

        auto path = map.pathfind(position, [&](glm::vec2 pos) {
            // Check if this walkable tile is adjacent to water/river
            auto neighbors = {
                glm::vec2(-1, 0), glm::vec2(1, 0),
                glm::vec2(0, -1), glm::vec2(0, 1)
            };
            for (auto offset : neighbors) {
                Tile* neighbor = map.getTile(pos + offset);
                if (neighbor && (neighbor->terrain == Tile::water || neighbor->terrain == Tile::river)) {
                    found = true;
                    return true;
                }
            }
            return false;
        });

        if (!found || path.tiles.empty()) {
            this->failure_reason = "no water nearby";
            return StageStatus::Failed;
        }

        this->reg.emplace<Path>(this->entity, std::move(path));
        return StageStatus::Complete;
    })
    .stage("walking to water",
        stage_primitives::wait_until_removed<Path>(reg, entity))
    .stage("casting line", [this](double) -> StageStatus {
        this->reg.emplace<Fishing>(this->entity, this->reg);
        return StageStatus::Complete;
    })
    .stage("fishing",
        stage_primitives::wait_until_removed<Fishing>(reg, entity));
```

## Action Header

```cpp
#ifndef FISH_ACTION_H
#define FISH_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>

namespace dy {

class FishAction : public ActionBase<FishAction> {
public:
    FishAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    ActionStageMachine machine_;
};

}

#endif
```

## Action Implementation

```cpp
std::unique_ptr<Action> FishAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();
    StageStatus status = machine_.tick(dt);
    if (status == StageStatus::Failed) return nullptr;
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string FishAction::describe() const {
    return "Fish: " + machine_.current_stage_name();
}
```

## Prerequisites

None.

## Failure Conditions

- `"no water nearby"` — no walkable tile adjacent to water/river within pathfinding range

## Memory Events

- Type: `"action_completed"`
- Description: `"Caught [amount] fish"`

## Example LLM JSON

```json
{
  "action": "Fish",
  "thought": "I'm hungry and there's a river nearby. Let me try fishing.",
  "speech": "Maybe I can catch something for dinner."
}
```
