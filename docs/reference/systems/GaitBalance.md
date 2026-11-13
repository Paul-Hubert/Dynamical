# GaitBalance

`GaitBalanceSys` in [`src/logic/agents/gait.cpp`](../../../src/logic/agents/gait.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after `GaitFootIK` / `GaitHandIK` and before `Fabrik` · Parameters: no

Places the pelvis of the kinematic walk: as high as the legs reach their targets, and where the centre of mass of the
solved pose sits on its target (`GaitState::com_target`). Leans the trunk as far as the planted hands need, and
measures the balance ([`BalanceState`](../components/BalanceState.md)) that the feet and `GaitChecks` use.

## Behaviour

- **tick**: returns until the feet are ready.
  - The arms of enabled hand targets are solved with the legs; each planted palm pulls the centre of mass target
    `hand_support * grip` of the way toward it. The trunk's lean for the hands decays over `trunk_return` s when no
    hand asks for it.
  - **Height**: no higher than both legs reach (`GaitState::reach` from the targets' ankle positions), no higher than
    the nominal hip height less a crouch `bias` held through the stride (what the last step lacked in reach at its
    worst, eased over `crouch_time`). Scrambling, the hips are lifted so that no knee folds past the shortest hip to
    ankle distance, no faster than 0.3 m/s and no higher than the legs reached through the last step.
  - **Iterations** (up to `balance_iterations`, until the error is under `balance_tolerance`): sets the pose's root
    position from the hip centre; with hands, leans the trunk about the bottom of the spine until the shoulders are
    within `hand_reach` of the arm's reach of the hands' targets (at most `trunk_reach`); solves the legs and arms on
    a copy of the pose (`solveChain` with the chains' limits and yield); moves the hips horizontally by the centre of
    mass error.
  - Writes into the pose: the root position and the leaned spine joints. A planted palm still out of reach after the
    lean is released (`Releasing`, wait 0.6 s when it held less than 0.25 s).
  - **Balance state**: crouch, offset from where `GaitBase` put the hips, lift, deficit; the centre of mass, its
    velocity (filtered over `velocity_smoothing`), the extrapolated centre of mass on the ground (`com + v / omega`),
    its lead along the heading; the support hull of the soles' last and next prints and the planted palms, how far the
    centre of mass is outside it, the lateral margin to the stance foot's outer edge; at a heel strike `captured` and
    the capture error against the rule.

## Parameters

None for the system: [`GaitParams`](../components/GaitParams.md) (`balance_*`, `crouch_time`, `velocity_smoothing`,
`hand_support`, `hand_reach`, `trunk_reach`, `trunk_return`) on the rig entity.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | read | ensureRig | the body file for the rig model |
| [HumanBotSpawn](../components/HumanBotSpawn.md) | component | read | ensureRig | finds the rig entity |
| [Rig](../components/Rig.md) | component | read+create | tick, ensureRig | the model: solves, forward kinematics, centre of mass |
| [RigPose](../components/RigPose.md) | component | readwrite+create | tick, ensureRig | the root position and the spine joints written |
| [GaitCommand](../components/GaitCommand.md) | component | write+create | tick (gaitRig) | only emplaced with its defaults when missing; not read |
| [GaitParams](../components/GaitParams.md) | component | read+create | tick (gaitRig) | balance and hand parameters |
| [GaitState](../components/GaitState.md) | component | readwrite+create | tick (gaitRig) | hip centre, pivot, targets read; `trunk_reach` and the hands' deficit, contact and timers written |
| [BalanceState](../components/BalanceState.md) | component | readwrite+create | tick | emplaced on first use; the balance of this tick |
| [IKChain](../components/IKChain.md) | component | read | tick | the legs' and arms' joints, poles, limits and yield |
| [IKTarget](../components/IKTarget.md) | component | read | tick | the legs' and arms' goals, `enabled` |
| [GroundField](../components/GroundField.md) | context | read+create | tick | the ground height under the centre of mass (a level plane is made when missing) |

## Side effects

- None beyond the components.

## Order and dependencies

- After `GaitFootIK` and `GaitHandIK` (this tick's targets); before `Fabrik`, which solves the same targets on the pose
  whose pelvis this system placed, and `RigPoseApply`.
- `GaitBase`, `GaitFootIK` and `GaitHandIK` of the next tick read the `BalanceState` it leaves (offset, lead, com).

## Used in scenes
<!-- generated -->

## Tests

- `GAIT.1` to `GAIT.14` in [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp) (centre of mass error, support, capture error, lateral margin)
