# GaitBase

`GaitBaseSys` in [`src/logic/agents/gait.cpp`](../../../src/logic/agents/gait.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, first of the gait (`GaitBase`, `GaitFootIK`, `GaitHandIK`, `GaitBalance`, `Fabrik`, `RigPoseApply`) · Parameters: no

The clock and the base animation of the kinematic walk (docs/physical-agents/ik-gait.md): follows the
[`GaitCommand`](../components/GaitCommand.md) at the rates the walker allows, slowed by the slope, advances the stride
phase and the walker's reference point on the ground, sets this tick's gait laws, and writes the pelvis, the trunk,
the head and the arms into the rig's [`RigPose`](../components/RigPose.md). The legs are left to the IK
(`GaitFootIK`, `Fabrik`), the pelvis' final place to `GaitBalance`.

## Behaviour

- **tick**: gets the rig (`gaitRig`: `ensureRig`, then `Rig` and `RigPose`, and `GaitCommand`, `GaitParams`,
  `GaitState` emplaced with their defaults when missing); returns without a rig entity.
  - **First tick** (`GaitState::started` false): the feet's geometry from the body file (sole frames, joints, toe-out),
    the leg length (hip to sole), the reach (`leg_extension` of thigh + shank), the shortest hip to ankle distance
    the knee allows (`fold`), the hip pivot in the pelvis frame; heading from the command, speed 0, the reference
    point on the ground under `GaitCommand::start` (a ray down from y = 4096), the phase in the middle of a double
    support; `RigPose::teleport = true`.
  - **Command**: the steepness under and up to `look_ahead` ahead of the walker; the wanted speed = command speed x
    `exp(-slope_slowdown * steepness)` (no less than `slope_speed_floor` of it), capped so that a step climbs no
    more than `step_rise`; 0 under `min_speed`. Speed moves toward it at `acceleration` / `braking`, heading toward
    the command's at `turn_rate`.
  - **Gait laws**: stride time from the step length law (`step_factor`, `step_exponent`, at most `max_stride_time`),
    step length, stance share by speed, amplitude, `omega = sqrt(g / com height)` (from the last
    [`BalanceState`](../components/BalanceState.md) when valid), the capture offset.
  - **Clock**: the phase advances while moving, or until both feet are down side by side (a stopping walker closes
    its last step). The reference point moves along the heading and is put on the ground.
  - **Slope**: pitch and roll of the ground along and across the heading (from the two prints once the feet are
    ready, else a ray cast), filtered over 0.3 s; the trunk's lean from the speed and the slope.
  - **Scrambling** (`GaitParams::scramble`): `low` rises from 0 to 1 between `scramble_from` and `scramble_full` of
    slope; the uphill direction. When lowered, the trunk tilts toward the slope until the shoulders come within
    `scramble_shoulder` of the ground (bisection on trial poses, the hips where `GaitBalance` had them), no further
    than `scramble_lean` or `scramble_past` past lying along the slope; when even that is not enough the centre of
    mass target moves up the slope (`forward`, up to `scramble_forward`).
  - **Pose**: hip centre over the reference point (sway toward the stance foot as an inverted pendulum, bob at each
    double support, crouch when lowered), centre of mass target (`com_target`). Pelvis: yaw of the heading, gait yaw
    and list, tipped toward the slope for what the spine cannot bend; `root_position` from the hip centre. Spine and
    Spine2: lean, counter-rotation of the shoulders, list undone, the tilt; Head: a level gaze. Arms: swing against
    the legs (`arm_swing`, `arm_bias`, `elbow`, `elbow_swing`), hanging under a tilted trunk. Every joint is clamped to
    the model's IK limits.

## Parameters

None for the system: the fields of [`GaitParams`](../components/GaitParams.md) and
[`GaitCommand`](../components/GaitCommand.md) on the rig entity (JSON in the scene's entity).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | read | ensureRig | the body file for the rig model |
| [HumanBotSpawn](../components/HumanBotSpawn.md) | component | read | ensureRig | finds the rig entity |
| [Rig](../components/Rig.md) | component | read+create | tick, ensureRig | the rig model: geometry, forward kinematics, limits |
| [RigPose](../components/RigPose.md) | component | readwrite+create | tick, ensureRig | pelvis, spine, head and arm joints written; `teleport` on the first tick |
| [GaitCommand](../components/GaitCommand.md) | component | read+create | tick (gaitRig) | speed, heading, start |
| [GaitParams](../components/GaitParams.md) | component | read+create | tick (gaitRig) | the gait's laws and shapes |
| [GaitState](../components/GaitState.md) | component | readwrite+create | tick (gaitRig) | phase, speed, heading, reference point, laws, slope, scrambling state |
| [BalanceState](../components/BalanceState.md) | component | read | tick | centre of mass height (omega) and the hips' offset of the last tick |
| [GroundField](../components/GroundField.md) | context | read+create | tick | heights and normals of the ground (a level plane is made when missing) |

## Side effects

- None beyond the components (no physics: the pose is applied by `RigPoseApply`).

## Order and dependencies

- After `AgentFactory` (the `HumanBot` and its kinematic bodies) and the ground system; after `GaitRoutes` when the
  world records demonstrations (a new route resets `GaitState`).
- Before `GaitFootIK`, `GaitHandIK`, `GaitBalance` (they read the state and the pose it wrote this tick), `Fabrik`,
  `RigPoseApply`. It reads the `BalanceState` of the previous tick.

## Used in scenes

<!-- generated -->
- [`common/ik_demo_world.json`](../../../resources/scenes/common/ik_demo_world.json): `fixed`
- [`common/ik_world.json`](../../../resources/scenes/common/ik_world.json): `fixed`

## Tests

- `GAIT.1` to `GAIT.14` in [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp) (scenes `ik_walk_slope`, `ik_walk_terrain`, `ik_check`, `ik_check_terrain` on `ik_world`)
- `TW.7`, `TW.9`, `TW.10` in [`tests/unit/terrain_walker.cpp`](../../../tests/unit/terrain_walker.cpp)
