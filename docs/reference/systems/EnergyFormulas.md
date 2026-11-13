# EnergyFormulas

`EnergyFormulasSys` in [`src/logic/agents/humanbot_formulas.cpp`](../../../src/logic/agents/humanbot_formulas.cpp) (registered from [`humanbot_energy.cpp`](../../../src/logic/agents/humanbot_energy.cpp)) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after `Energy` · Parameters: no

The three energy formulas of a long rollout (docs/physical-agents/energy-formulas.md), eval only: the change of the
Lagrangian and of the body energy, the joint work from a recursive Newton-Euler pass on the measured kinematics and
Jolt's contact impulses, and the motor work, together with checks of each against what Jolt applied. Read only:
nothing here feeds the reward or the physics. The totals are summed over the worlds by the eval host.

## Behaviour

**tick** (every tick, not while parked). Returns unless the bot is built, dynamic and `view_truncate` (eval).

1. First call: a `ContactRecorder` (`JPH::ContactImpulseListener`, patched Jolt) set on the world's
   `PhysicsSystem`, kept in the `ContactRecorderHandle` context variable; it gathers the solved contact impulses of
   every step.
2. `L = sum (T - V)` and `E = sum (T + V)` at the end of the step. Joint order children before parents, once.
3. When the books are valid: per segment the contact impulse and angular impulse from the recorded reports (normal
   per point, friction at the friction point, torsion), and their work. Newton-Euler leaf to root on impulses over the
   step at the start-of-step pose: each joint's force and torque. Motor torque from `GetTotalLambdaMotor`; per joint
   RNE work and drive work. Checks: joint force against Jolt's point lambda, torque against the motor (free joints)
   or the limits' share (a limit lambda is non-zero), what the recursion leaves on the root, contact impulse against
   the momentum balance, angular velocity clamps, energy closure with and without the point constraints' work.
   DeVita stance windows per limb on the recorded normal impulse (sagittal and 3D, RNE and drive). After
   `statsSettleSeconds`: Lagrangian and body energy deltas, per-joint works, torque variation (`books`), ticks,
   seconds, metres along the target direction, all into `FormulaBooks::totals`.
4. Reports cleared; this step's state kept for the next.

## Parameters

None; `statsSettleSeconds`, `totalMass`, `maxAngularVelocity` from the params.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick | state, constraints, `books.tick_torque_variation` read; `formulas` written |
| [ContactRecorderHandle](../components/ContactRecorderHandle.md) | context | readwrite+create | tick | the contact listener and its reports |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | tick | `SetContactImpulseListener` once, gravity |

## Side effects

- Installs a contact impulse listener on the world's Jolt `PhysicsSystem` (never removed; one per world, training
  worlds step on one thread).

## Order and dependencies

- After `PhysicsStep`, `BodySync` and `Energy`. The eval host (`src/train/train_host.cpp`) adds every world's
  `formulas.totals` into its summary.

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/humanbot.cpp`: "Energy formulas: Newton-Euler recovers Jolt's impulses, the motor axes are Jolt's, the
  Lagrangian matches the bodies".
