# Fabrik

`FabrikSys` in [`src/logic/agents/ik.cpp`](../../../src/logic/agents/ik.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after the gait systems and before `RigPoseApply` · Parameters: no

The inverse kinematics of the rigs (docs/physical-agents/ik-gait.md), component driven: any system describes what it
wants as [`IKTarget`](../components/IKTarget.md) entities for an [`IKChain`](../components/IKChain.md); this system
solves every chain that has enabled targets, in one batch, and writes the joint angles into the rig's
[`RigPose`](../components/RigPose.md). It knows nothing of gaits or terrain.

## Behaviour

- **tick**:
  - `ensureRig(reg)`: makes sure the world's rig entity (the one with
    [`HumanBotSpawn`](../components/HumanBotSpawn.md)) has a [`Rig`](../components/Rig.md) and a `RigPose`, when the
    world has a [`HumanBot`](../components/HumanBot.md) with a body.
  - Gathers the targets per chain: enabled, weight > 0, and a valid `chain` entity.
  - For each `IKChain` (its `solved` reset to false): skipped without targets or when its `rig` entity lacks `Rig` or
    `RigPose`. Among its targets the highest `priority` wins; those of that priority blend by weight: position and pole
    are weighted means (the pole normalised), rotations a weighted sum of quaternions brought into the first one's
    hemisphere, normalised (`use_rotation` when any of them uses it).
  - Solves with `solveChain(model, pose, joints, pole_rest, goal, limits, yield)`: FABRIK on the joints' anchors (pole
    side kept for the inner joints), then bone rotations from the root, each clamped to the model's IK limits when
    `limits`; the tip joint takes the target rotation. With `yield`, when the clamped solution misses the target by
    more than 0.5 mm or 0.2°, it solves again without limits (`yielded`).
  - When the total weight is under 1, the chain's joints are slerped from the pose before by that weight.
  - Writes the result into the chain: `solved`, `iterations`, `position_error`, `rotation_error`, `limit_excess`,
    `yielded`.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [IKTarget](../components/IKTarget.md) | component | read | tick | what each chain must reach |
| [IKChain](../components/IKChain.md) | component | readwrite | tick | joints, rest pole, limits, yield read; the solve's results written |
| [Rig](../components/Rig.md) | component | read+create | tick, ensureRig | the rig model of the chain's rig; created by `ensureRig` on first use |
| [RigPose](../components/RigPose.md) | component | readwrite+create | tick, ensureRig | the chains' joint angles written; created at the spawn pose by `ensureRig` |
| [HumanBotSpawn](../components/HumanBotSpawn.md) | component | read | ensureRig | finds the rig entity |
| [HumanBot](../components/HumanBot.md) | context | read | ensureRig | its body file, for the rig model |

## Side effects

- None (no physics: `RigPoseApply` moves the bodies).

## Order and dependencies

- After the systems that post targets this tick: `GaitFootIK`, `GaitHandIK`, and after `GaitBalance`, which places the
  pelvis (its own solves are on a copy of the pose; this one writes the pose that is applied).
- Before `RigPoseApply`, which moves the kinematic bodies to the solved pose.

## Used in scenes

<!-- generated -->
- [`common/ik_demo_world.json`](../../../resources/scenes/common/ik_demo_world.json): `fixed`
- [`common/ik_world.json`](../../../resources/scenes/common/ik_world.json): `fixed`

## Tests

- `IK.1` to `IK.6` in [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp) (`fabrik`, limits, `solveChain`); every `GAIT.*` test runs it in the lane
