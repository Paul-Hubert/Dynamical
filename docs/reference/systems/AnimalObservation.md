# AnimalObservation

`AnimalObservationSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list as `AnimalObservation@observe`, after `AnimalRewards@observe` · Parameters: no

Writes the walker's observation for the next decision: two stacked frames of `7 + 9 x segments` values (the command,
the speed error, the height error, the soft depth, then the forward axis, up axis and origin velocity of every
segment in the trunk's heading frame), the order sensor with orders, and takes the decision snapshot the next
window is rewarded against.

## Behaviour

- **tick** (last tick of each window): returns when the bot is not built.
  - Snapshot: trunk position, command direction and speed, mode and head offsets (what `AnimalRewards` rates the
    next window against).
  - With orders and a sensor: `observeOrder` into `io.sensors[0]` (`nogail_order`): head yaw / 1 rad, pitch / 1.1 rad,
    the mode one-hot (5).
  - Recording (kinematic demonstrations): `io.recordable = warm_left <= 0 && !demo_restart`; the first recorded
    decision of a walk empties the stack; `bot.recorded++` per recordable decision.
  - `observeAnimal`: header `[0..1]` the command velocity in the heading frame (forward, right) in walk speeds, `[2]`
    turn x length, `[3..4]` the command minus the trunk's horizontal velocity, `[5]` `1 - trunk height / standing
    height` clamped to [-1, 1], `[6]` soft depth in heights; per segment the -Z axis, the +Y axis and the origin
    velocity / walk speed, projected in the trunk's heading frame. Non finite values become 0.
  - `io.observation` = previous frame then current frame; the current one is kept as `previous_frame`.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | tick | state read; snapshots, `previous_frame`, `recorded` written |
| [AgentIO](../components/AgentIO.md) | context | readwrite | tick | `observation`, `sensors[0]`, `recordable` written (sensors checked first) |

## Side effects

- None beyond the registry.

## Order and dependencies

- After `AnimalRewards@observe` (which rates against the previous snapshot and may reset the episode), before
  `AnimalEpisode@observe`. The host reads `AgentIO` after the observe phase.

## Used in scenes
<!-- generated -->

## Tests

- `AA.3` (layout: header, nine values per segment, stacked twice; first observation of an episode), `AA.9` (soft depth in the observation), `AA.6` (observed axes never jump), in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp).
