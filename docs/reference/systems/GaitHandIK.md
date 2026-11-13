# GaitHandIK

`GaitHandIKSys` in [`src/logic/agents/gait.cpp`](../../../src/logic/agents/gait.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, between `GaitFootIK` and `GaitBalance` (template `common/ik_world.json`) · Parameters: no

Scrambling (`GaitParams::scramble`, scene variable `scramble`, off by default; docs/physical-agents/ik-gait.md): from
20° to 50° of slope `GaitBase` brings the trunk down toward the ground, and this system lays the palm of a hand that
comes within reach flat on the ground, on prints fixed in the world, keeps it while the arm reaches, then moves on.
It owns the arms' [`IKChain`](../components/IKChain.md) and [`IKTarget`](../components/IKTarget.md) entities.

## Behaviour

- **tick**: returns until the gait has started, and while `scramble` is off and the hands were never made ready (so
  with scrambling off it costs nothing and creates nothing).
  - **First tick** (`hands_ready` false): the hands' geometry (palm as a sole, shoulder, elbow and wrist joints, the
    arm's reach, the elbow's rest pole); a chain entity per arm (`"left arm"` / `"right arm"`, joints shoulder, elbow,
    wrist) and a target entity, owner `"GaitHandIK"`, **disabled**.
  - The trial pose: the trunk where `GaitBase` put it, the hips moved by the last `BalanceState::offset`, arms swinging.
  - **Per hand**, a state machine `Free` → `Reaching` → `Planted` → `Releasing` → `Free`:
    - Free or releasing, scrambling, not waiting: looks for a hold (`findHold`: points ahead of and beside the
      shoulder within `hand_plant_reach` of the arm, the palm fitted flat on the ground no steeper than
      `hand_max_tilt`, facing it from the shoulder's side; the furthest ahead while moving, the nearest a relaxed arm
      when standing; the fingers turned to where the arm is least outside its limits) and reaches for it.
    - Planted: lets go when the arm is out of reach (`deficit` of the last `GaitBalance`, or the wrist more than the
      arm's reach + 0.3 m away) or scrambling stopped, taking another hold when there is one; a hold that lasted less
      than 0.25 s makes the hand wait 0.6 s. While moving, after `hand_hold` s and when the other hand is not reaching,
      moves on to a hold ahead when the wrist is `hand_behind` behind the shoulder or the arm more than `hand_over`
      outside its limits (and clearly better off there).
    - Reaching: a hold that went out of reach on the way is replaced, or dropped (releasing, wait 0.6 s).
    - The palm moves over `hand_time` s, lifted by `hand_lift` on its way; `grip` eases to 1 when planted.
  - Writes the arm's target (enabled unless the hand is free): the wrist anchor, the palm's rotation, an elbow pole
    chosen among 12 around the shoulder-wrist line for the least limit excess near a relaxed elbow, turning smoothly;
    the chain's `yield` = `yield_limits`.

## Parameters

None for the system: [`GaitParams`](../components/GaitParams.md) (`scramble`, `hand_*`) on the rig entity.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | read | ensureRig | the body file for the rig model |
| [HumanBotSpawn](../components/HumanBotSpawn.md) | component | read | ensureRig | finds the rig entity |
| [Rig](../components/Rig.md) | component | read+create | tick, ensureRig | the model: arm geometry, trial solves, limits |
| [RigPose](../components/RigPose.md) | component | read+create | tick, ensureRig | copied for the trial pose |
| [GaitCommand](../components/GaitCommand.md) | component | write+create | tick (gaitRig) | only emplaced with its defaults when missing; not read |
| [GaitParams](../components/GaitParams.md) | component | read+create | tick (gaitRig) | `scramble`, the hands' reach, timing and tilt |
| [GaitState](../components/GaitState.md) | component | readwrite+create | tick (gaitRig) | the hands' state (contact, prints, grip, pole, deficit), `hands_ready`; heading, uphill, `low`, `moving` read |
| [BalanceState](../components/BalanceState.md) | component | read | tick | the hips' offset of the last tick |
| [IKChain](../components/IKChain.md) | component | write+create | tick | the arms' chains created; `yield` set every tick |
| [IKTarget](../components/IKTarget.md) | component | write+create | tick | the arms' targets created, enabled or not, written every tick |
| [GroundField](../components/GroundField.md) | context | read+create | tick | holds fitted on the ground (a level plane is made when missing) |

## Side effects

- Entity creation: two chain entities and two target entities the first time scrambling is on.

## Order and dependencies

- After `GaitFootIK` and `GaitBase` (the trunk's tilt and `low`); before `GaitBalance`, which solves the arms of the
  enabled targets with the legs, leans the trunk for their reach, and may release a palm (`HandState::contact`), and
  before `Fabrik`.

## Used in scenes

<!-- generated -->
- [`common/ik_world.json`](../../../resources/scenes/common/ik_world.json): `fixed`

## Tests

- `GAIT.12`, `GAIT.13`, `GAIT.14` in [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp)
