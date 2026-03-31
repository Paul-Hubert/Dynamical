# Mine Action

## Overview

Extract stone or ore from deposits in the world. Mirrors the HarvestAction pattern exactly: find deposit, pathfind, emplace `Mine` component, wait for `MineSys` to process.

## ActionID

`ActionID::Mine`

## Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `target_type` | `std::string` | `"stone_deposit"` | `"stone_deposit"` or `"ore_deposit"` |
| `resource` | `std::string` | `"stone"` | What to mine: `"stone"` or `"ore"` |

## Infrastructure Changes

### Item::ID Additions

**File**: `src/logic/components/item.h`

```cpp
enum ID : int {
    null,
    berry,
    wood,    // introduced by Craft, but listed here for completeness
    stone,   // NEW: mined from stone_deposit
    ore,     // NEW: mined from ore_deposit
    // ... rest
};
```

### Object::Identifier Additions

**File**: `src/logic/components/object.h`

```cpp
enum Type {
    plant,
    being,
    resource,   // NEW: for minable deposits
    building
};

enum Identifier {
    tree,
    berry_bush,
    human,
    bunny,
    stone_deposit,  // NEW
    ore_deposit,    // NEW
    // ...
};
```

### New Component: `Mine`

**File**: `src/logic/components/mine.h`

```cpp
#ifndef MINE_H
#define MINE_H

#include <entt/entt.hpp>
#include <logic/components/time.h>

namespace dy {

class Mine {
public:
    Mine(entt::registry& reg, entt::entity deposit) : deposit(deposit) {
        start = reg.ctx<dy::Time>().current;
    }
    entt::entity deposit;
    time_t start;
};

}

#endif
```

### New Component: `Mined`

**File**: `src/logic/components/mined.h`

Cooldown tag for deposits (mirrors `Harvested` for berry bushes):

```cpp
#ifndef MINED_H
#define MINED_H

#include "time.h"

namespace dy {

class Mined {
public:
    Mined(time_t end) : end(end) {}
    time_t end;
};

}

#endif
```

### New System: `MineSys`

**File**: `src/logic/systems/mine.cpp`

**Registration**:
- Add `DEFINE_SYSTEM(MineSys)` to `src/logic/systems/system_list.h`
- Add `set->pre_add<MineSys>()` to `src/logic/game.cpp` after `EatSys`

```cpp
#include "system_list.h"

#include <logic/components/mine.h>
#include <logic/components/mined.h>
#include <logic/components/storage.h>
#include <logic/components/item.h>
#include <logic/components/object.h>
#include <logic/components/action_bar.h>

using namespace entt::literals;
using namespace dy;

void MineSys::preinit() {}
void MineSys::init() {}

void MineSys::tick(double dt) {
    OPTICK_EVENT();

    constexpr time_t duration = 15 * Time::minute;

    auto& time = reg.ctx<Time>();

    auto view = reg.view<Mine>();
    view.each([&](const auto entity, auto& mine) {
        if (!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, mine.start, mine.start + duration);
        }

        if (time.current > mine.start + duration) {
            auto& storage = reg.get<Storage>(entity);

            // Determine yield based on deposit type
            int yield = 5 + (std::rand() % 6);  // 5-10 items
            if (reg.valid(mine.deposit) && reg.all_of<Object>(mine.deposit)) {
                auto& obj = reg.get<Object>(mine.deposit);
                if (obj.id == Object::ore_deposit) {
                    storage.add(Item(Item::ore, yield));
                } else {
                    storage.add(Item(Item::stone, yield));
                }
                // Apply cooldown to deposit (1 day)
                reg.emplace<Mined>(mine.deposit, time.current + Time::day);
            }

            reg.remove<Mine>(entity);
            reg.remove<ActionBar>(entity);
        }
    });

    // Clear expired Mined cooldowns
    auto view2 = reg.view<Mined>();
    view2.each([&](const auto entity, auto& mined) {
        if (time.current > mined.end) {
            reg.remove<Mined>(entity);
        }
    });
}

void MineSys::finish() {}
```

### Factory Functions

**File**: `src/logic/factories/factory_list.h` — add declarations:

```cpp
DEFINE_FACTORY(StoneDeposit, glm::vec2 position)
DEFINE_FACTORY(OreDeposit, glm::vec2 position)
```

**File**: `src/logic/factories/factory_list.cpp` — add implementations:

```cpp
entt::entity dy::buildStoneDeposit(entt::registry& reg, glm::vec2 position) {
    return dy::buildObject(reg, position, Object::resource, Object::stone_deposit,
                           glm::vec2(1, 1), Color(0.5, 0.5, 0.5, 1.0));
}

entt::entity dy::buildOreDeposit(entt::registry& reg, glm::vec2 position) {
    return dy::buildObject(reg, position, Object::resource, Object::ore_deposit,
                           glm::vec2(1, 1), Color(0.6, 0.4, 0.2, 1.0));
}
```

### Map Generator Changes

**File**: `src/logic/map/map_generator.cpp`

Add deposit spawning in the chunk generation loop. Since `stone` tiles have speed 0.0 (impassable), place deposits on grass tiles near steep slopes:

```cpp
// After existing tree/berry_bush spawning block:
if (tile.terrain == Tile::grass) {
    // ... existing tree/bush spawning ...

    // Stone deposits on grass tiles near steep terrain
    auto neighbours = { glm::ivec2(-1,0), glm::ivec2(1,0), glm::ivec2(0,1), glm::ivec2(0,-1) };
    bool near_stone = false;
    for (auto v : neighbours) {
        Tile* n = map.getTile(position + glm::vec2(v));
        if (n && n->terrain == Tile::stone) { near_stone = true; break; }
    }

    if (near_stone && tile.object == entt::null) {
        if (frand() < 0.005) {
            tile.object = dy::buildStoneDeposit(reg, position + glm::vec2(frand(), frand()));
        } else if (frand() < 0.001) {
            tile.object = dy::buildOreDeposit(reg, position + glm::vec2(frand(), frand()));
        }
    }
}
```

## Stages

```
machine_
    .stage("finding deposit", [this](double) -> StageStatus {
        if (this->reg.all_of<Path>(this->entity))
            this->reg.remove<Path>(this->entity);

        find_deposit();

        if (target_ == entt::null) {
            this->failure_reason = "no deposits available";
            return StageStatus::Failed;
        }

        this->reg.emplace<entt::tag<"reserved"_hs>>(target_);
        return StageStatus::Complete;
    })
    .stage("navigating to deposit",
        stage_primitives::wait_until_removed<Path>(reg, entity))
    .stage("begin mining", [this](double) -> StageStatus {
        this->reg.emplace<Mine>(this->entity, this->reg, target_);
        return StageStatus::Complete;
    })
    .stage("mining",
        stage_primitives::wait_until_removed<Mine>(reg, entity));
```

### Helper: `find_deposit()`

```cpp
void MineAction::find_deposit() {
    auto& map = reg.ctx<MapManager>();
    auto position = reg.get<Position>(entity);

    Object::Identifier target_id = Object::stone_deposit;
    if (params.target_type == "ore_deposit") target_id = Object::ore_deposit;

    target_ = entt::null;

    reg.emplace<Path>(entity, map.pathfind(position, [&](glm::vec2 pos) {
        auto object = map.getTile(pos)->object;
        if (object != entt::null
            && reg.all_of<Object>(object)
            && reg.get<Object>(object).id == target_id
            && !reg.all_of<entt::tag<"reserved"_hs>>(object)
            && !reg.all_of<Mined>(object)) {
            target_ = object;
            return true;
        }
        return false;
    }));
}
```

## Action Header

```cpp
#ifndef MINE_ACTION_H
#define MINE_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <logic/components/object.h>

namespace dy {

class MineAction : public ActionBase<MineAction> {
public:
    MineAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    void find_deposit();
    entt::entity target_ = entt::null;
    ActionStageMachine machine_;
};

}

#endif
```

## Action Implementation

```cpp
std::unique_ptr<Action> MineAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();

    StageStatus status = machine_.tick(dt);

    if (status == StageStatus::Complete) {
        reg.remove<entt::tag<"reserved"_hs>>(target_);
        return nullptr;
    }

    if (status == StageStatus::Failed) {
        return nullptr;
    }

    return self;
}

std::string MineAction::describe() const {
    return "Mine: " + machine_.current_stage_name();
}
```

## Prerequisites

None currently. Future: require `stone_pickaxe` for ore deposits.

```cpp
// Future prerequisite for ore mining:
{
    .description = "needs pickaxe for ore",
    .is_satisfied = [](auto& r, auto e) {
        if (!r.template all_of<Storage>(e)) return false;
        return r.template get<Storage>(e).amount(Item::stone_pickaxe) > 0;
    },
    .resolve_action = [](auto& r, auto e) { return ActionID::Craft; }
}
```

## Failure Conditions

- `"no deposits available"` — no unreserved, un-mined deposits found within pathfinding range

## Memory Events

- Type: `"action_completed"`
- Description: `"Mined [amount] [stone/ore] from a deposit"`

## Example LLM JSON

```json
{
  "action": "Mine",
  "target_type": "stone_deposit",
  "resource": "stone",
  "thought": "I need stone to build a house.",
  "speech": "Time to do some mining."
}
```

```json
{
  "action": "Mine",
  "target_type": "ore_deposit",
  "resource": "ore",
  "thought": "Ore is valuable. I should collect some.",
  "speech": "Let me dig for some ore."
}
```
