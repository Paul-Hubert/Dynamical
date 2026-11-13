# SphereObserve

`SphereObserveSys` in [`src/logic/agents/sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, `SphereObserve@observe` (last tick of a decision window) · Parameters: no

The observe phase of the sphere roller: closes the decision window (its reward, the end of the episode, the stats)
and writes the 16 float observation of the live target, whose velocity becomes the target the next window is rated
against.

## Behaviour

- **tick** (last tick of each window): finds the `SphereAgent` + `Transform` + `RigidBody` entity, returns without one.
  - **Trainer RESET** (`AgentIO::reset_requested`): clears the flag, calls `resetSphereEpisode` (see
    [SphereControl](SphereControl.md): spawn on the ground at rest, a fresh target, `AgentIO` cleared) and, with
    `stagger`, starts the episode at a random decision count in [0, `max_decisions`) so that worlds that start
    together do not all end (and rebuild their ground) together.
  - **Otherwise, unless a reset is pending**: `AgentIO::reward` = mean of the per-tick rewards of the window (0 without
    any), stats `Sphere/Speed error` (mean horizontal speed error); counts the decision and ends the episode at
    `max_decisions` (not for a steered agent). Every end is a truncation: `done = truncated = ended`; at an end it adds
    the stat `Sphere/Out of bounds` (1 or 0), sets `pending_reset` (the new episode starts at the next window, in
    `SphereControl`) and un-parks the world. Clears the window's sums.
  - **Observation** (always, `SphereObs` layout, every vector in the goal frame of the live target: y up, z along the
    target heading): the ground normal under the sphere, `probe_distance` ahead along the horizontal velocity (along
    the target when slower than 0.05 m/s) and ahead along the target direction (ray casts from `radius + 2` m up,
    zero where there is no ground), the linear and angular velocities, the target speed.
  - Stores `decision_target = targetVelocity()`, `decision_heading = heading`, `has_decision = !ended`.

## Parameters

None for the system: the [`SphereAgent`](../components/SphereAgent.md) fields (`max_decisions`, `stagger`,
`steered`, `probe_distance`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [SphereAgent](../components/SphereAgent.md) | component | readwrite | tick, resetSphereEpisode | window sums read and cleared, decision count, end, decision target |
| [Transform](../components/Transform.md) | component | readwrite | tick (read), resetSphereEpisode (write) | the position for the probes; the spawn on a RESET |
| [RigidBody](../components/RigidBody.md) | component | read | tick, resetSphereEpisode | the Jolt body id, the radius |
| [PhysicsPose](../components/PhysicsPose.md) | component | write | resetSphereEpisode | set to the spawn on a RESET (when present) |
| [AgentIO](../components/AgentIO.md) | context | readwrite | tick, resetSphereEpisode | reset request, reward, done, truncated, stats, observation |
| [WorldRng](../components/WorldRng.md) | context | readwrite | tick (stagger), resetSphereEpisode | the staggered start, spawn point and first target |
| [WorldClock](../components/WorldClock.md) | context | read | resetSphereEpisode | simulation time for the colliders |
| [WorldInfo](../components/WorldInfo.md) | context | read | resetSphereEpisode | spawn centre (optional) |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | tick (read), resetSphereEpisode (write) | velocities and ground ray casts; the teleport on a RESET |
| [TerrainColliders](../components/TerrainColliders.md) | context | readwrite | resetSphereEpisode | colliders of the spawn column (optional) |
| [Parked](../components/Parked.md) | context | write | tick, resetSphereEpisode | un-parked at the end of an episode (when present) |

## Side effects

- Three downward ray casts against the world's static bodies per window (`PhysicsWorld::groundBelow`).
- On a RESET, the effects of `resetSphereEpisode`: the Jolt body is teleported and the marching cubes colliders of the
  spawn column are built synchronously.

## Order and dependencies

- Runs as `@observe`, after `SphereTask` (the window's sums and the end flags) and after `PhysicsStep`.
- What it stores (`decision_heading`, `has_decision`, `pending_reset`) is used by `SphereControl` on the first tick of
  the next window. The training host reads `AgentIO` right after this phase.

## Used in scenes

<!-- generated -->
- [`common/sphere_world.json`](../../../resources/scenes/common/sphere_world.json): `fixed@observe`
- [`common/sphere_world_hills.json`](../../../resources/scenes/common/sphere_world_hills.json): `fixed@observe`

## Tests

- `SR.4`, `SR.5`, `SR.7`, `SR.12`, `SR.17` in [`tests/unit/sphere_roller.cpp`](../../../tests/unit/sphere_roller.cpp)
- `SR.9` in [`tests/protocol/sphere_env.cpp`](../../../tests/protocol/sphere_env.cpp)
