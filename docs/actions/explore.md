# Explore Action

## Overview

Discover new areas by traveling to a distant point. An extended version of Wander with longer range (40-80 tiles vs 20) and AIMemory event recording of what was found at the destination.

## ActionID

`ActionID::Explore`

## Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `direction` | `std::string` | `""` | Optional: `"north"`, `"south"`, `"east"`, `"west"`. If empty, picks random direction |

## Infrastructure Changes

None. Uses existing `Path` component, `MapManager::pathfind()`, and `AIMemory`.

## Stages

```
machine_
    .stage("choosing direction", [this](double) -> StageStatus {
        auto& map = this->reg.ctx<MapManager>();
        auto position = this->reg.get<Position>(this->entity);

        // Determine direction offset
        glm::vec2 offset(0, 0);
        float distance = 40.0f + (std::rand() % 40);  // 40-80 tiles

        if (this->params.direction == "north") offset = {0, -distance};
        else if (this->params.direction == "south") offset = {0, distance};
        else if (this->params.direction == "east") offset = {distance, 0};
        else if (this->params.direction == "west") offset = {-distance, 0};
        else {
            float angle = (std::rand() % 360) * 3.14159f / 180.0f;
            offset = {std::cos(angle) * distance, std::sin(angle) * distance};
        }

        glm::vec2 goal = glm::vec2(position.x, position.y) + offset;
        target_pos_ = goal;

        // Pathfind toward the goal — find closest walkable tile near target
        auto path = map.pathfind(position, [&](glm::vec2 pos) {
            return glm::distance(pos, goal) < 5.0f;
        });

        if (path.tiles.empty()) {
            this->failure_reason = "no path to explore in that direction";
            return StageStatus::Failed;
        }

        this->reg.emplace<Path>(this->entity, std::move(path));
        return StageStatus::Complete;
    })
    .stage("exploring",
        stage_primitives::wait_until_removed<Path>(reg, entity))
    .stage("recording observations", [this](double) -> StageStatus {
        auto& position = this->reg.get<Position>(this->entity);
        auto& map = this->reg.ctx<MapManager>();

        // Survey destination area
        std::string observations;
        auto* tile = map.getTile(glm::vec2(position.x, position.y));
        if (tile) {
            switch (tile->terrain) {
                case Tile::grass: observations = "grassland"; break;
                case Tile::sand: observations = "sandy shore"; break;
                case Tile::stone: observations = "rocky terrain"; break;
                case Tile::water: observations = "open water"; break;
                case Tile::river: observations = "a river"; break;
                default: observations = "unknown terrain"; break;
            }
        }

        if (auto* memory = this->reg.try_get<AIMemory>(this->entity)) {
            std::string dir = this->params.direction.empty() ? "a new area" : "the " + this->params.direction;
            memory->add_event("explored", "Explored " + dir + " and found " + observations);
        }
        return StageStatus::Complete;
    });
```

## Action Header

```cpp
#ifndef EXPLORE_ACTION_H
#define EXPLORE_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <glm/glm.hpp>

namespace dy {

class ExploreAction : public ActionBase<ExploreAction> {
public:
    ExploreAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    ActionStageMachine machine_;
    glm::vec2 target_pos_{0, 0};
};

}

#endif
```

## Action Implementation

```cpp
#include "explore_action.h"

#include <extra/optick/optick.h>
#include <logic/map/map_manager.h>
#include <logic/components/path.h>
#include <logic/components/position.h>
#include <logic/map/tile.h>
#include <ai/memory/ai_memory.h>

using namespace dy;

ExploreAction::ExploreAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    // stages as above
}

std::unique_ptr<Action> ExploreAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();
    StageStatus status = machine_.tick(dt);
    if (status == StageStatus::Failed) return nullptr;
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string ExploreAction::describe() const {
    return "Explore: " + machine_.current_stage_name();
}
```

## Prerequisites

None. Explore is always available.

## Failure Conditions

- `"no path to explore in that direction"` — pathfinding fails (surrounded by water/stone)

## Memory Events

- Type: `"explored"`
- Description: `"Explored the [direction] and found [terrain description]"`

## Example LLM JSON

```json
{
  "action": "Explore",
  "direction": "north",
  "thought": "I wonder what's beyond those hills to the north.",
  "speech": "Let me see what's out there..."
}
```

Random direction:
```json
{
  "action": "Explore",
  "thought": "I feel like discovering something new today.",
  "speech": "Adventure awaits!"
}
```
