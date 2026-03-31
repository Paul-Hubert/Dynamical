# Flee Action

## Overview

Run away from a perceived threat. The entity calculates the opposite direction from the threat and pathfinds 15+ tiles away. Non-interruptible once started.

## ActionID

`ActionID::Flee`

## Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `target_entity` | `entt::entity` | `entt::null` | The threat entity to flee from |
| `target_name` | `std::string` | `""` | Name of the threat (for finding by name) |
| `direction` | `std::string` | `""` | Override: flee in this direction instead of calculating |

## Infrastructure Changes

None. Uses existing `Path` component, `MapManager::pathfind()`, and `Position`.

## Stages

```
machine_
    .stage("calculating escape", [this](double) -> StageStatus {
        this->interruptible = false;

        auto& map = this->reg.ctx<MapManager>();
        auto& my_pos = this->reg.get<Position>(this->entity);
        glm::vec2 my_xy(my_pos.x, my_pos.y);

        // Find the threat
        glm::vec2 threat_pos = my_xy;
        bool found_threat = false;

        if (this->params.target_entity != entt::null
            && this->reg.valid(this->params.target_entity)
            && this->reg.all_of<Position>(this->params.target_entity)) {
            auto& tp = this->reg.get<Position>(this->params.target_entity);
            threat_pos = {tp.x, tp.y};
            found_threat = true;
        }

        // If no specific target, find nearest being
        if (!found_threat) {
            float closest = 999999.0f;
            auto view = this->reg.view<Object, Position>();
            for (auto [e, obj, pos] : view.each()) {
                if (e == this->entity) continue;
                if (obj.type != Object::being) continue;
                float d = glm::distance(my_xy, glm::vec2(pos.x, pos.y));
                if (d < closest) {
                    closest = d;
                    threat_pos = {pos.x, pos.y};
                    found_threat = true;
                }
            }
        }

        if (!found_threat) {
            this->failure_reason = "nothing to flee from";
            return StageStatus::Failed;
        }

        // Calculate opposite direction
        glm::vec2 away = glm::normalize(my_xy - threat_pos);
        float flee_distance = 15.0f + (std::rand() % 10);
        glm::vec2 goal = my_xy + away * flee_distance;

        auto path = map.pathfind(my_pos, [&](glm::vec2 pos) {
            return glm::distance(pos, goal) < 5.0f;
        });

        if (path.tiles.empty()) {
            // Just go anywhere walkable far from threat
            path = map.pathfind(my_pos, [&](glm::vec2 pos) {
                return glm::distance(pos, threat_pos) > flee_distance;
            });
        }

        if (path.tiles.empty()) {
            this->failure_reason = "nowhere to flee";
            return StageStatus::Failed;
        }

        if (this->reg.all_of<Path>(this->entity))
            this->reg.remove<Path>(this->entity);

        this->reg.emplace<Path>(this->entity, std::move(path));
        return StageStatus::Complete;
    })
    .stage("fleeing",
        stage_primitives::wait_until_removed<Path>(reg, entity));
```

## Action Header

```cpp
#ifndef FLEE_ACTION_H
#define FLEE_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>

namespace dy {

class FleeAction : public ActionBase<FleeAction> {
public:
    FleeAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
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
#include "flee_action.h"

#include <extra/optick/optick.h>
#include <logic/map/map_manager.h>
#include <logic/components/path.h>
#include <logic/components/position.h>
#include <logic/components/object.h>

using namespace dy;

FleeAction::FleeAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    // stages as above
}

std::unique_ptr<Action> FleeAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();
    StageStatus status = machine_.tick(dt);
    if (status == StageStatus::Failed) return nullptr;
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string FleeAction::describe() const {
    return "Flee: " + machine_.current_stage_name();
}
```

## Prerequisites

None.

## Failure Conditions

- `"nothing to flee from"` — no threat entity found and no nearby beings
- `"nowhere to flee"` — pathfinding fails in all directions (cornered)

## Memory Events

On completion:
- Type: `"action_completed"`
- Description: `"Fled from danger"`

On failure:
- Type: `"action_failed"`
- Description: `"Failed to flee: [reason]"`

## Example LLM JSON

```json
{
  "action": "Flee",
  "target_name": "Entity#7",
  "thought": "That entity looks dangerous. I should run.",
  "speech": "Stay away from me!"
}
```

Generic flee (nearest threat):
```json
{
  "action": "Flee",
  "thought": "Something feels wrong here. Time to go.",
  "speech": "I need to get out of here!"
}
```
