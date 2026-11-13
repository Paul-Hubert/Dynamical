# AnimalDrive

`AnimalDriveSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list as `AnimalDrive@act`, after `TrainerActions@act`, before `PhysicsStep` · Parameters: no

Turns the decision of the window (`AgentIO::action`, 8 values a joint) into the motor settings of the ragdoll's
swing-twist constraints: spring and damper gains, the target orientation and the target angular velocity. A zero
action holds the standing pose at about half gains.

## Behaviour

- **tick** (first tick of each decision window): returns when the bot is not built or is kinematic (the procedural
  walk moves those bodies). Else `driveAnimalBot(bot, io.action, 1)`, for every joint `j` of the body, with its values
  `a[0..7]` clamped to [-1, 1] (a non finite value counts as 0):
  - gains: spring = `0.02 + 0.98 (a0 + 1) / 2`, damper = `0.02 + 0.98 (a1 + 1) / 2` (never 0: a spring without
    stiffness is a rigid one to Jolt); swing and twist motors get `stiffness x spring`, `damping x damper` and the
    torque limit `max_torque` of the body file's `AnimalJointDrive`;
  - motor state `PositionAndVelocity`, or `Off` when `bot.limp` (the standing test); on a change of state the friction
    torque is set (`0.0005 x max_torque` limp, 0 driven);
  - target: twist, swing about y and swing about z each from its lower limit (-1) through the standing pose (0) to its
    upper limit (+1), as `constraintRotation(twist, swing_y, swing_z)`; when Jolt clamps the target to the cone the
    bot's `target_clamps` counter goes up;
  - target angular velocity `(a5, a6, a7) x max_target_angular_velocity` in constraint space.
  Then `bot.decisions++` and `bot.window_ticks = 0`.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | tick (driveAnimalBot) | the body's drives and limits, the constraints; `decisions`, `window_ticks`, `target_clamps` updated |
| [AgentIO](../components/AgentIO.md) | context | read | tick | `action`, written by `TrainerActions` (or a policy) |

## Side effects

- Changes the motor settings, motor states, friction and targets of the ragdoll's Jolt constraints (through the
  pointers the bot holds, not through the registry).

## Order and dependencies

- After `TrainerActions@act` (writes the action), before `AnimalSoftGround` and `PhysicsStep`.
- Its `window_ticks = 0` starts the window that `AnimalSync` counts and `AnimalRewards` divides by.

## Used in scenes
<!-- generated -->

## Tests

- `AA.2` (a zero action stands 10 s, limp falls), `AA.4` (deterministic stepping), `AA.5` (limp ragdoll ended), in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp).
