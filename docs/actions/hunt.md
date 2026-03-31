# Hunt Action

## Overview

Track and hunt an animal for meat. The entity pathfinds to the nearest prey (bunny by default), stalks it for a few seconds, then collects meat and destroys the animal entity. No new ECS system required — uses timed wait and entity destruction.

## ActionID

`ActionID::Hunt`

## Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `target_type` | `std::string` | `"bunny"` | Animal identifier to hunt |

## Infrastructure Changes

### Item::ID Addition

**File**: `src/logic/components/item.h`

```cpp
meat,   // NEW: dropped by hunted animals
```

## Stages

```
machine_
    .stage("finding prey", [this](double) -> StageStatus {
        if (this->reg.all_of<Path>(this->entity))
            this->reg.remove<Path>(this->entity);

        find_prey();

        if (target_ == entt::null) {
            this->failure_reason = "no prey found";
            return StageStatus::Failed;
        }

        this->reg.emplace<entt::tag<"reserved"_hs>>(target_);
        return StageStatus::Complete;
    })
    .stage("stalking",
        stage_primitives::wait_until_removed<Path>(reg, entity))
    .stage("hunting",
        stage_primitives::wait_for_seconds(5.0))
    .stage("collecting meat", [this](double) -> StageStatus {
        auto& storage = this->reg.get<Storage>(this->entity);
        int yield = 2 + (std::rand() % 3);  // 2-4 meat
        storage.add(Item(Item::meat, yield));

        // Record what was hunted for memory
        std::string prey_name = this->params.target_type.empty() ? "an animal" : "a " + this->params.target_type;

        // Destroy the prey entity
        if (this->reg.valid(target_)) {
            dy::destroyObject(this->reg, target_);
        }

        if (auto* memory = this->reg.try_get<AIMemory>(this->entity)) {
            memory->add_event("hunted", "Hunted " + prey_name + " and collected " + std::to_string(yield) + " meat");
        }

        return StageStatus::Complete;
    });
```

### Helper: `find_prey()`

```cpp
void HuntAction::find_prey() {
    auto& map = reg.ctx<MapManager>();
    auto position = reg.get<Position>(entity);

    // Determine target animal
    Object::Identifier prey_id = Object::bunny;  // default
    // Could extend to other animal types based on params.target_type

    target_ = entt::null;

    reg.emplace<Path>(entity, map.pathfind(position, [&](glm::vec2 pos) {
        auto object = map.getTile(pos)->object;
        if (object != entt::null
            && reg.all_of<Object>(object)
            && reg.get<Object>(object).id == prey_id
            && reg.get<Object>(object).type == Object::being
            && !reg.all_of<entt::tag<"reserved"_hs>>(object)) {
            target_ = object;
            return true;
        }
        return false;
    }));
}
```

## Action Header

```cpp
#ifndef HUNT_ACTION_H
#define HUNT_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <logic/components/object.h>

namespace dy {

class HuntAction : public ActionBase<HuntAction> {
public:
    HuntAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    void find_prey();
    entt::entity target_ = entt::null;
    ActionStageMachine machine_;
};

}

#endif
```

## Action Implementation

```cpp
std::unique_ptr<Action> HuntAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();

    StageStatus status = machine_.tick(dt);

    if (status == StageStatus::Complete || status == StageStatus::Failed) {
        // Clean up reservation if target still exists
        if (target_ != entt::null && reg.valid(target_)
            && reg.all_of<entt::tag<"reserved"_hs>>(target_)) {
            reg.remove<entt::tag<"reserved"_hs>>(target_);
        }
        return nullptr;
    }

    return self;
}

std::string HuntAction::describe() const {
    return "Hunt: " + machine_.current_stage_name();
}
```

## Prerequisites

None.

## Failure Conditions

- `"no prey found"` — no huntable animals of the specified type within pathfinding range

## Memory Events

- Type: `"hunted"`
- Description: `"Hunted a [animal] and collected [amount] meat"`

## Example LLM JSON

```json
{
  "action": "Hunt",
  "target_type": "bunny",
  "thought": "I need meat for cooking. There should be bunnies nearby.",
  "speech": "Time to hunt!"
}
```
