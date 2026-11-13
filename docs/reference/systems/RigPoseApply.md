# RigPoseApply

`RigPoseApplySys` in [`src/logic/agents/ik.cpp`](../../../src/logic/agents/ik.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after `Fabrik` and before `PhysicsStep` · Parameters: no

Forward kinematics of every [`RigPose`](../components/RigPose.md) onto the kinematic Jolt bodies of the world's
[`HumanBot`](../components/HumanBot.md): what turns the joint angles of the kinematic walk into moving bodies that
`BodySync`, the observations and the recorders then read.

## Behaviour

- **tick**: returns until the `HumanBot` is built. When the bot is not kinematic (`HumanBotSpawn` `motion` other than
  `kinematic`) it logs an error once per process and returns.
- For every entity with [`Rig`](../components/Rig.md) + `RigPose`: computes the world pose of every segment
  (`RigModel::forward`), and for each of the bot's bodies (segment order) either sets its position and rotation
  (`SetPositionAndRotation`, activated) when `pose.teleport` is set or `h <= 0`, or moves it there over the tick
  (`MoveKinematic`, which gives the body the velocities that reach the pose in `h`). Then clears `teleport`.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | read | tick | `built`, `kinematic`, the Jolt body ids |
| [Rig](../components/Rig.md) | component | read | tick | the rig model (forward kinematics) |
| [RigPose](../components/RigPose.md) | component | readwrite | tick | the pose applied; `teleport` cleared |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | tick | the body interface: kinematic moves and teleports |

## Side effects

- Moves (or teleports) every kinematic Jolt body of the bot each tick.
- Log: an error, once per process (a function-static flag shared by every world), when the bot is not kinematic.

## Order and dependencies

- After `Fabrik` (the solved pose) and every gait system; before `PhysicsStep`, so that the kinematic move happens in
  this tick's step; `BodySync` after the step reads the bodies back into the bot's `RagdollState`, which `GaitChecks`
  measures.
- `GaitBase` sets `teleport` when a gait starts (and `GaitRoutes` makes that happen at every route), so that the bodies
  jump to the new place rather than sweep there.

## Used in scenes

<!-- generated -->
- [`common/ik_demo_world.json`](../../../resources/scenes/common/ik_demo_world.json): `fixed`
- [`common/ik_world.json`](../../../resources/scenes/common/ik_world.json): `fixed`

## Tests

- Every `GAIT.*` test in [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp) (`GAIT.8` measures the joint speeds of the moved bodies, `GAIT.9` the observation lane on them)
- `TW.7`, `TW.9`, `TW.10` in [`tests/unit/terrain_walker.cpp`](../../../tests/unit/terrain_walker.cpp)
