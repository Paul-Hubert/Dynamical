# GaitFootIK

`GaitFootIKSys` in [`src/logic/agents/gait.cpp`](../../../src/logic/agents/gait.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after `GaitBase` · Parameters: no

Plants the feet of the kinematic walk on the [`GroundField`](../components/GroundField.md): footsteps where the
balance rule wants them, heel strike, flat stance and toe-off on prints fixed in the world, swing arcs over the
ground. It owns the legs' [`IKChain`](../components/IKChain.md) and [`IKTarget`](../components/IKTarget.md) entities
and writes each target every tick; `Fabrik` solves them.

## Behaviour

- **tick**: returns until `GaitBase` has started the gait.
  - **First tick** (`feet_ready` false): each foot's print fitted on the ground `step_width / 2` to its side of the
    reference point, standing; a chain entity per leg (`IKChain`: rig, `"left leg"` / `"right leg"`, joints hip, knee,
    ankle, rest pole -z) and a target entity (`IKTarget`, owner `"GaitFootIK"`). The ankles' IK limits are reset to
    the body file's and widened by `ankle_roll_extra` / `ankle_yaw_extra` (this changes the shared `RigModel`).
  - **Ground changed** (`GroundField::version`): the planted prints are fitted again where they are; landings are
    planned again.
  - The pose as it will be: the hips where `GaitBase` put them, moved as `GaitBalance` moved them on the last tick.
  - **Per foot** by its phase: toe-off when the phase passes the stance share (the swing starts from where the rocker
    left the foot), heel strike when it comes back (`landed`). In stance: heel rocker, flat, toe rocker
    (`flat_start`, `heel_off`, strike and toe angles); soles settle flat when standing; the heel rises early for a
    trailing foot whose leg would be too short for a flat sole (bisection), or as far as it can; the ankle's
    dorsiflexion stop raises the heel too (`ankle_margin`, `relief`), eased down at `heel_drop_rate`. In swing: until
    `replan_until` the landing is planned (heel `capture_offset` behind the extrapolated centre of mass, on the path
    turned toward the commanded heading; brought back toward the last print when it would be more than
    `max_print_rise` above or below the other foot; flatter uphill and no flatter than the ankle allows); the lift
    that clears the ground by `swing_clearance`; the ankle's stop and swing cone held in the air.
  - Writes the leg's target: the ankle anchor's position, the foot's rotation, the knee pole (between the pelvis'
    forward and the foot's, `knee_follow`, `knee_out`), enabled; the chain's `yield` = `yield_limits`.

## Parameters

None for the system: [`GaitParams`](../components/GaitParams.md) on the rig entity.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | read | ensureRig | the body file for the rig model |
| [HumanBotSpawn](../components/HumanBotSpawn.md) | component | read | ensureRig | finds the rig entity |
| [Rig](../components/Rig.md) | component | readwrite+create | tick, ensureRig | the model: geometry and trial solves; the ankles' IK limits widened on the first tick |
| [RigPose](../components/RigPose.md) | component | read+create | tick, ensureRig | copied for trial solves (ankle stops, reach) |
| [GaitCommand](../components/GaitCommand.md) | component | read+create | tick (gaitRig) | the commanded heading for the landing plan |
| [GaitParams](../components/GaitParams.md) | component | read+create | tick (gaitRig) | stride shapes, rockers, swing, limits |
| [GaitState](../components/GaitState.md) | component | readwrite+create | tick (gaitRig) | the feet's state: prints, contact, pitch, swing, pose; `feet_ready`, `ground_version` |
| [BalanceState](../components/BalanceState.md) | component | read | tick | the hips' offset and the lead of the extrapolated centre of mass (last tick) |
| [IKChain](../components/IKChain.md) | component | write+create | tick | the legs' chains created; `yield` set every tick |
| [IKTarget](../components/IKTarget.md) | component | write+create | tick | the legs' targets created and written every tick |
| [GroundField](../components/GroundField.md) | context | read+create | tick | prints fitted, swing clearance, heights (a level plane is made when missing) |

## Side effects

- Entity creation: two chain entities and two target entities on the first tick of a gait (again after `GaitRoutes`
  replaced the `GaitState`).

## Order and dependencies

- After `GaitBase` (state, pose and laws of this tick); before `GaitHandIK`, `GaitBalance` (which solves the legs with
  these targets to place the pelvis) and `Fabrik` (which solves them on the applied pose).

## Used in scenes
<!-- generated -->

## Tests

- `IK.7` (`fitFootprint`, `footPose`) and `GAIT.1` to `GAIT.14` in [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp)
- `TW.7`, `TW.9`, `TW.10` in [`tests/unit/terrain_walker.cpp`](../../../tests/unit/terrain_walker.cpp)
