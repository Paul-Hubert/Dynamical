# BodySync

`BodySyncSys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, right after `PhysicsStep` · Parameters: no

Reads the HumanBot ragdoll back from Jolt after each physics step: the world state of every segment
(`RagdollState`), the finite-difference velocities of the joint anchors that the observations carry, the tick
counters, and the poses of the drawn shapes.

## Behaviour

**tick** (every tick, not while parked). Returns when the bot is not built.

1. `readState`: position, rotation, linear and angular velocity of each segment body (`GetPositionAndRotation`,
   `GetLinearVelocity`, `GetAngularVelocity`) into `HumanBot::state`.
2. For every joint, the anchor is the child segment's origin: `anchor_velocity = (anchor - anchor_previous) / h` when
   the history is valid and `h > 0`, then `anchor_previous = anchor`; `history_valid = true`.
3. `tick++`, `window_ticks++` (the episode's and the window's tick counts).
4. With `draw`: every `WorldShape` + `Transform` entity gets the pose of its segment times its local pose, in
   `Transform` and in `PhysicsPose` (created on first use; previous pose kept for the viewer's interpolation).

It works the same on a kinematic bot (the rig moved by `TrackActions` or [RigPoseApply](RigPoseApply.md)).

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick | `state`, `tick`, `window_ticks` written |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | read | tick (`readState`) | body poses and velocities |
| [WorldShape](../components/WorldShape.md) | component | read | tick (draw) | segment and local pose of each drawn shape |
| [Transform](../components/Transform.md) | component | write | tick (draw) | the shape's world pose |
| [PhysicsPose](../components/PhysicsPose.md) | component | readwrite+create | tick (draw) | previous and current pose for interpolation |

## Side effects

- None besides the components (adds `PhysicsPose` to the drawn shape entities).

## Order and dependencies

- Right after `PhysicsStep`: `Energy`, `EnergyFormulas`, `Task`, `Termination`, `DeVitaStepStat`, the observe phase
  and [GaitChecks](GaitChecks.md) all read the state it writes.
- `DriveTargets` resets `window_ticks` at the act phase; `Rewards` divides by it.

## Used in scenes

<!-- generated -->
- [`common/humanbot_terrain_world.json`](../../../resources/scenes/common/humanbot_terrain_world.json): `fixed`
- [`common/humanbot_world.json`](../../../resources/scenes/common/humanbot_world.json): `fixed`
- [`common/ik_demo_world.json`](../../../resources/scenes/common/ik_demo_world.json): `fixed`
- [`common/ik_world.json`](../../../resources/scenes/common/ik_world.json): `fixed`
- [`common/rig_world.json`](../../../resources/scenes/common/rig_world.json): `fixed`

## Tests

- `tests/unit/humanbot.cpp`: S8.1 (oracle layout reproduces Unity's observations), S4.1, S4.3.
- `tests/unit/humanbot_rig.cpp`: S5.2 to S5.5, T9.2 / T9.4 (recorded observations).
- `tests/unit/terrain_walker.cpp`, `tests/unit/ik_gait.cpp` through the walker and IK scenes.
