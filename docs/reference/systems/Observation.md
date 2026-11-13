# Observation

`ObservationSys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list as `Observation@observe`, after `Rewards@observe` · Parameters: no

Writes the HumanBot walker's observation (layout `heidelbot_notoes_2`, two stacked frames of 138 floats) and the
decision snapshot: the command of the coming window, against which `Rewards` will score it.

## Behaviour

**tick** (observe phase, also while parked). Returns when the bot is not built.

1. Snapshot: spine position, `target_speed`, `tsv = horizontal direction to the target x target_speed`.
2. `observeV2` (138 floats): header = target velocity minus the spine's horizontal velocity in the spine's yaw-only
   heading frame (x, z) and the head error `clamp(1 - height / est0, 0, 1)`; the command is zero while dying or
   recording, and the velocity difference too while recording with `speedDiffZeroRecording`. Then per joint 9
   floats in the heading frame of its parent (the spine for the pelvis' children): the child's Unity primary axis,
   its secondary axis, and the anchor's finite-difference velocity.
3. Non-finite values -> 0. `AgentIO::observation` = previous frame then current frame (276 floats); the current
   frame becomes the previous one.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick | state read; `snapshot` and `previous_frame` written |
| [AgentIO](../components/AgentIO.md) | context | write | tick | `observation` |

## Side effects

- None.

## Order and dependencies

- After `Rewards@observe` (which reads the old snapshot) and before `Episode@observe`. In the IK scenes
  (`ik_world`) it runs alone in the observe phase so that the kinematic walk can be recorded as demonstrations.

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/humanbot.cpp`: T8.1 (heading frames orthogonal and yaw only), S8.1 (oracle layout reproduces Unity's
  observations).
- `tests/unit/humanbot_rig.cpp`: T9.2 / T9.4 (recorded observations: header zeroed, no reset spike).
