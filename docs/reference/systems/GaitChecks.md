# GaitChecks

`GaitChecksSys` in [`src/logic/agents/gait.cpp`](../../../src/logic/agents/gait.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after `PhysicsStep` and `BodySync` · Parameters: no

Measures the kinematic walk as the bodies actually stand after the physics step: the soles and palms against the
ground and their prints, the joints against the IK and the body file's limits, the IK residuals, the balance. It
writes this tick's values and the worst since `settle` seconds into [`GaitReport`](../components/GaitReport.md),
which the tests and the gait viewer (`src/view/gait_view.cpp`) read.

## Behaviour

- **tick**: returns until the feet are ready and the [`HumanBot`](../components/HumanBot.md) is built. Reads the
  segments' poses from the bot's `RagdollState` (written by `BodySync`).
  - **Feet**: for each sole corner, distance to the ground (penetration), the contact error of the planted corners
    (by `FootContact`: heel corners at heel contact, toe corners at toe contact, all four flat), the slip of a planted
    corner since it was planted (planted points reset at a heel strike), the angle between a flat sole and its print;
    steps counted at heel strikes; the flatness gap of the stance prints. Planted points are forgotten when the
    ground's version changed.
  - **Joints**: the worst excess over the IK limits and over the body file's limits, and the worst joint.
  - **IK**: the legs' residuals (position, rotation) in stance and in swing, the limits taken away (`limit_excess`),
    the most heel rise an ankle stop asked for; the capture error at a heel strike.
  - **Hands** (once they are ready): the same contact, penetration and slip measures for planted palms, the arms' IK
    residuals, ticks planted and holds taken per hand and the worst arm limit excess (after `settle`).
  - Counts the tick; once `GaitState::time >= settle`: the maxima and minima (`max_*`, `min_lateral_margin`,
    `min_hip_height`, `max_low`), ticks on which a leg's limits gave way (`yielded`), and the mean speed of the
    centre of mass since the settle time.

## Parameters

None for the system: `GaitReport::settle` (2 s by default) is the start of the measured stretch.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | read | tick, ensureRig | `built`; the bodies' poses (`state`); the body file |
| [HumanBotSpawn](../components/HumanBotSpawn.md) | component | read | ensureRig | finds the rig entity |
| [Rig](../components/Rig.md) | component | read+create | tick, ensureRig | geometry, joint frames, IK and body file limits |
| [RigPose](../components/RigPose.md) | component | write+create | ensureRig | only created by `ensureRig`; not read |
| [GaitCommand](../components/GaitCommand.md) | component | write+create | tick (gaitRig) | only emplaced with its defaults when missing; not read |
| [GaitParams](../components/GaitParams.md) | component | write+create | tick (gaitRig) | only emplaced with its defaults when missing; not read |
| [GaitState](../components/GaitState.md) | component | read+create | tick (gaitRig) | the feet's and hands' contact, prints, time |
| [BalanceState](../components/BalanceState.md) | component | read | tick | centre of mass, capture, support, margins |
| [IKChain](../components/IKChain.md) | component | read | tick | the solve's residuals, limit excess, `yielded`, `solved` |
| [GaitReport](../components/GaitReport.md) | component | readwrite+create | tick | emplaced on first use; this tick's measures and the worst since `settle` |
| [GroundField](../components/GroundField.md) | context | read+create | tick | the ground under each corner (a level plane is made when missing) |

## Side effects

- None (measures only).

## Order and dependencies

- After `PhysicsStep` and `BodySync`: it measures the bodies where the step left them (the kinematic moves of
  `RigPoseApply`), not the pose that was asked for.
- `GaitRoutes` clears the report's planted points and settle time at each new route.

## Used in scenes

<!-- generated -->
- [`common/ik_demo_world.json`](../../../resources/scenes/common/ik_demo_world.json): `fixed`
- [`common/ik_world.json`](../../../resources/scenes/common/ik_world.json): `fixed`

## Tests

- `GAIT.1` to `GAIT.14` in [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp) (their checks are on `GaitReport`)
- `TW.7`, `TW.10` in [`tests/unit/terrain_walker.cpp`](../../../tests/unit/terrain_walker.cpp)
