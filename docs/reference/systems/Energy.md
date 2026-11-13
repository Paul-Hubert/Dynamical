# Energy

`EnergySys` in [`src/logic/agents/humanbot_energy.cpp`](../../../src/logic/agents/humanbot_energy.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, right after `BodySync` · Parameters: no

The energy books of the HumanBot walker for the physics step just done (docs/physical-agents/physics.md): the
whole-body energy, the exact work of every joint motor from Jolt's motor impulses, each segment's contact impulse from
the momentum balance, and the residual between them. Feeds the energy reward of `Rewards`, the stance detection of
`DeVitaStepStat` and the closure checks of the eval summary.

## Behaviour

**tick** (every tick, not while parked). Returns when the bot is not built or kinematic.

1. `E = sum over the 16 segments of 1/2 m v² + 1/2 ω·Iω - m g·com` at the end of the step (masses and world inertia
   from the body file scaled to the episode's `totalMass`).
2. When the books are valid (not the first tick of an episode), per joint: the motor axes = the constraint frame
   carried by the child body at the start of the step (`GetConstraintToBody2`), impulse = `GetTotalLambdaMotor` on
   them, relative angular velocity at mid step, `work = impulse · ω_mid`; `torque = impulse / dt`; the sum of
   `|torque - previous torque|` (torque variation); positive and negative work. Per segment: contact impulse =
   momentum change - gravity impulse - the joints' linear (`GetTotalLambdaPosition`) impulses, and its work on the
   mid-step velocity. Fills `tick_pos`, `tick_neg`, `tick_motor`, `tick_contact`, `tick_energy` (dE),
   `tick_residual = dE - motor - contact`, adds to `window_pos`, `window_neg`, `window_torque_variation` and the
   cumulative `cum_abs_work`, `cum_residual`, `cum_net_residual`.
3. Otherwise (first tick): joint works, contact impulses and torque history cleared.
4. Stores `E`, `valid = true`, and this step's velocities and rotations for the next one.

`Rewards` zeroes the window sums at every observe phase; `resetHumanBot` invalidates the books.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick | `state`, `params`, `body`, constraints read; `books` written |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | read | tick | gravity of the world's config |

## Side effects

- None (reads Jolt's constraint lambdas through the `HumanBot`'s constraint references).

## Order and dependencies

- After `PhysicsStep` and `BodySync` (it needs this step's state and lambdas), before `DeVitaStepStat`
  (contact impulses, joint works) and `Rewards@observe` (window sums), and before `EnergyFormulas` reads
  `tick_torque_variation`.

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/humanbot.cpp`: "The torque-rate term sums the motor torque changes over the window".
- `tests/unit/humanbot_rig.cpp`: S7.5 (closure of the energy books under random actions).
