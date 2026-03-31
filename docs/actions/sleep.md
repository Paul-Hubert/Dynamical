# Sleep Action

## Overview

Rest to restore energy. The entity lies down at its current location and sleeps for a configurable duration. Energy is fully restored upon completion.

## ActionID

`ActionID::Sleep`

## Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `duration` | `float` | `0.0` | Sleep duration in game-time seconds. If 0, defaults to `8 * Time::hour` |

## Infrastructure Changes

### BasicNeeds Extension

**File**: `src/logic/components/basic_needs.h`

Add an `energy` field that Sleep restores:

```cpp
class BasicNeeds {
public:
    float hunger = 0;
    float energy = 10;  // NEW: 0-10 scale, drained over time
};
```

### BasicNeedsSys Update

**File**: `src/logic/systems/basic_needs.cpp`

Drain energy at the same rate as hunger (10 units per game day):

```cpp
view.each([&](BasicNeeds& needs) {
    needs.hunger += 10.0 / (Date::seconds_in_a_minute * Date::minutes_in_an_hour * Date::hours_in_a_day) * time.dt;
    needs.energy -= 10.0 / (Date::seconds_in_a_minute * Date::minutes_in_an_hour * Date::hours_in_a_day) * time.dt;
    if (needs.energy < 0) needs.energy = 0;
});
```

### New Component: `Sleeping`

**File**: `src/logic/components/sleeping.h`

Mirrors the `Harvest`/`Eat` component pattern:

```cpp
#ifndef SLEEPING_H
#define SLEEPING_H

#include <entt/entt.hpp>
#include <logic/components/time.h>

namespace dy {

class Sleeping {
public:
    Sleeping(entt::registry& reg, time_t duration) : duration(duration) {
        start = reg.ctx<dy::Time>().current;
    }
    time_t start;
    time_t duration;
};

}

#endif
```

### New System: `SleepSys`

**File**: `src/logic/systems/sleep.cpp`

**Registration**:
- Add `DEFINE_SYSTEM(SleepSys)` to `src/logic/systems/system_list.h`
- Add `set->pre_add<SleepSys>()` to `src/logic/game.cpp` after `EatSys`

```cpp
#include "system_list.h"

#include <logic/components/sleeping.h>
#include <logic/components/basic_needs.h>
#include <logic/components/action_bar.h>

using namespace dy;

void SleepSys::preinit() {}
void SleepSys::init() {}

void SleepSys::tick(double dt) {
    OPTICK_EVENT();

    auto& time = reg.ctx<Time>();

    auto view = reg.view<Sleeping>();
    view.each([&](const auto entity, auto& sleeping) {
        if (!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, sleeping.start, sleeping.start + sleeping.duration);
        }

        if (time.current > sleeping.start + sleeping.duration) {
            if (reg.all_of<BasicNeeds>(entity)) {
                reg.get<BasicNeeds>(entity).energy = 10;
            }
            reg.remove<Sleeping>(entity);
            reg.remove<ActionBar>(entity);
        }
    });
}

void SleepSys::finish() {}
```

## Stages

```
machine_
    .stage("lying down", [this](double) -> StageStatus {
        time_t duration = static_cast<time_t>(this->params.duration);
        if (duration == 0) duration = 8 * Time::hour;
        this->reg.emplace<Sleeping>(this->entity, this->reg, duration);
        return StageStatus::Complete;
    })
    .stage("sleeping",
        stage_primitives::wait_until_removed<Sleeping>(reg, entity));
```

## Action Header

```cpp
#ifndef SLEEP_ACTION_H
#define SLEEP_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>

namespace dy {

class SleepAction : public ActionBase<SleepAction> {
public:
    SleepAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
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
#include "sleep_action.h"

#include <extra/optick/optick.h>
#include <logic/components/sleeping.h>
#include <logic/components/time.h>

using namespace dy;

SleepAction::SleepAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    machine_
        .stage("lying down", [this](double) -> StageStatus {
            time_t duration = static_cast<time_t>(this->params.duration);
            if (duration == 0) duration = 8 * Time::hour;
            this->reg.emplace<Sleeping>(this->entity, this->reg, duration);
            return StageStatus::Complete;
        })
        .stage("sleeping",
            stage_primitives::wait_until_removed<Sleeping>(reg, entity));
}

std::unique_ptr<Action> SleepAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();
    StageStatus status = machine_.tick(dt);
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string SleepAction::describe() const {
    return "Sleep: " + machine_.current_stage_name();
}
```

## Prerequisites

None. Sleep is always available.

```cpp
// In action_registry_init.cpp — no changes needed (already has empty prerequisites)
```

## Failure Conditions

None. Sleep always succeeds.

## Memory Events

On completion, record in `AISys::tick()` (already handled generically for all actions):
- Type: `"action_completed"`
- Description: `"Slept and restored energy"`

## Example LLM JSON

```json
{
  "action": "Sleep",
  "thought": "I'm exhausted from all that mining. Need to rest.",
  "speech": "Time for a nap..."
}
```

With custom duration:
```json
{
  "action": "Sleep",
  "duration": 14400,
  "thought": "Just a short rest.",
  "speech": "A quick power nap."
}
```
