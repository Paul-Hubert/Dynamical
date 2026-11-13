# DriveTargets

`DriveTargetsSys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list as `DriveTargets@act`, after `TrainerActions@act`, before `PhysicsStep` · Parameters: no

The act phase of the HumanBot walker: turns the window's action (8 floats per joint, 15 joints) into the gains and
targets of the Jolt swing-twist motors, which then hold them for the whole decision window.

## Behaviour

**tick** (first tick of each decision window, not while the world is parked). Returns at once when the `HumanBot` is
not built or kinematic. For every joint `j`, with `a = AgentIO::action[8j .. 8j+7]` (each value clamped to [-1, 1],
NaN / infinity -> 0):

- spring `s = (a0 + 1) / 2`, damper `d = (a1 + 1) / 2`; `scale = params.torque_factor x joint.torque_factor`;
  stiffness `= rotation_spring x s x scale x joint.spring_factor x drive_gain_scale`, damping
  `= rotation_damper x d x scale x joint.damper_factor x drive_gain_scale`, torque limit `= max_torque x scale`, the
  same on the swing and the twist motor (`StiffnessAndDamping` springs).
- Target orientation in constraint space: twist `= mid + a2 x half` of `[twist_min, twist_max]`, swing
  `(a3 x swing_y, a4 x swing_z)` as one rotation about `(0, y, z)`; `q = swing x twist` set with
  `SetTargetOrientationCS`. When Jolt clamps it to the cone (dot < 0.99999 with the requested one),
  `HumanBot::target_clamps` counts it.
- Target angular velocity `(a5, a6, a7) x max_target_angular_velocity` with `SetTargetAngularVelocityCS`.

Then `decisions++` and `window_ticks = 0` (BodySync counts the window's ticks again).

## Parameters

None. Gains and limits come from `HumanBot::params` (`rotationSpring`, `rotationDamper`, `maxTorque`,
`torqueFactor`, `maxTargetAngularVelocity`, `drive_gain_scale`, per-joint factors of the body file), refreshed by the
environment parameters at every episode start.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick | params, body, constraints read; `target_clamps`, `decisions`, `window_ticks` written |
| [AgentIO](../components/AgentIO.md) | context | read | tick | `action`, written by the host (or a policy system) before the act phase |

## Side effects

- Changes the motor settings and targets of the walker's 15 Jolt `SwingTwistConstraint`s.

## Order and dependencies

- After whatever writes `AgentIO::action` (the training host between windows, `TrainerActions@act` as a placeholder,
  a `Policy` system in viewer scenes), before `PhysicsStep`.
- `Rewards` reads the same action for its per-joint stats.

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/humanbot.cpp`: T6.3 (action zero is mid twist, full deflection the limits), S4.3.
- `tests/unit/humanbot_joints.cpp`: S6.2 (step responses against Unity, the gain scale), T6.1, S6.4 (standing at
  full gains).
