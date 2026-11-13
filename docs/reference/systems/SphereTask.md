# SphereTask

`SphereTaskSys` in [`src/logic/agents/sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after `PhysicsStep` (every tick) · Parameters: no

The task of the sphere roller, every physics tick: rates the sphere's velocity against the target the policy saw,
lets the live target drift toward a goal heading and speed, and ends the episode when the sphere leaves the field or
falls.

## Behaviour

- **tick**: finds the `SphereAgent` + `Transform` + `RigidBody` entity; returns when there is none or its Jolt body
  does not exist yet.
  - Reads the body's linear velocity. When the agent has a decision, adds `sphereReward(v, decision_target,
    reward_scale)` = `exp(-|v_xz - target| / scale)` to `reward_sum`, the horizontal speed error to `error_sum`, and
    counts the tick (`reward_ticks`). The target is the one of the last observation, not the live one.
  - Unless the agent is `steered`, counts `goal_timer` down and draws a new goal when it runs out: a heading within
    `max_goal_turn` of the current one, or back toward `home` (± 45°) once beyond `home_radius`; a speed from
    `speed_range` or 0 with `stop_chance`; a timer from `goal_interval`.
  - Turns the live `heading` toward `goal_heading` by at most `turn_rate * h` and moves `speed` toward `goal_speed` by
    at most `acceleration * h`.
  - Beyond `bounds` in |x| or |z| (when > 0) or more than `fall_depth` under `home`: sets `ended` and
    `out_of_bounds` and parks the world (`Parked::value = true`) until the observe phase.

## Parameters

None for the system: the [`SphereAgent`](../components/SphereAgent.md) fields (`reward_scale`, `turn_rate`,
`acceleration`, `goal_interval`, `max_goal_turn`, `home_radius`, `speed_range`, `stop_chance`, `bounds`,
`fall_depth`, `steered`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [SphereAgent](../components/SphereAgent.md) | component | readwrite | tick | reward sums, target drift, goal draws, end flags |
| [Transform](../components/Transform.md) | component | read | tick | the sphere's position (bounds, fall, home) |
| [RigidBody](../components/RigidBody.md) | component | read | tick | the Jolt body id |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | read | tick | the body's linear velocity |
| [WorldRng](../components/WorldRng.md) | context | readwrite | tick (drawGoal) | goal heading, speed and interval |
| [Parked](../components/Parked.md) | context | write | tick | parks the world when the episode ends off the field (when present) |

## Side effects

- None beyond the components: no log, no bodies touched.

## Order and dependencies

- After `PhysicsStep`: the velocity of this tick's step, and the `Transform` that the step wrote back from the body.
- Before `SphereObserve@observe`, which turns the sums into the window's reward and the end flag into `done`.

## Used in scenes

<!-- generated -->
- [`common/sphere_world.json`](../../../resources/scenes/common/sphere_world.json): `fixed`
- [`common/sphere_world_hills.json`](../../../resources/scenes/common/sphere_world_hills.json): `fixed`

## Tests

- `SR.2`, `SR.4`, `SR.6`, `SR.7`, `SR.17` in [`tests/unit/sphere_roller.cpp`](../../../tests/unit/sphere_roller.cpp)
- `SR.9` in [`tests/protocol/sphere_env.cpp`](../../../tests/protocol/sphere_env.cpp)
