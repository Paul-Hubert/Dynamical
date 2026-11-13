# Termination

`TerminationSys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after `Task` · Parameters: no

Decides when a HumanBot episode ends: killed when the walker has been low for too long (a fall), truncated at the
time limit, at the end of a recording, or when no route is left on the terrain. A kill parks the world for the rest
of the decision window.

## Behaviour

**tick** (every tick, not while parked). Returns when the bot is not built.

1. `dying = isDying()`: `currentHeight() < dyingHeightFactor x est0` (the head over the lower foot; on a ground field
   each one's height over the ground under it, so a slope does not make the walker shorter).
2. Recording: `truncated` once `tick >= max_recording_ticks`; nothing else.
3. Dying with `dieOnFall`: `++dying_ticks > maxDyingFrames` (0 by default: killed on the first dying tick) sets
   `killed`, closes the eval leg as fallen, sets `Parked` (the world's every-tick and act systems stop until the
   observe phase) and returns. Not dying with `resetOnAlive`: `dying_ticks = 0`.
4. `truncated` when `tick >= episodeTicks` (`maxFrames` at 60 Hz converted to the world's rate; never in view mode
   without `view_truncate`) or `route_lost`; a truncation closes the eval leg.

`Rewards` and `Episode` turn `killed` / `truncated` into the reward and `done` at the observe phase.

## Parameters

None; `dyingHeightFactor` (0.7), `dieOnFall` (true), `maxDyingFrames` (0), `resetOnAlive` (true), `maxFrames` (2000)
from the params file / env params.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick | `dying`, `dying_ticks`, `killed`, `truncated`, `legs` |
| [WorldClock](../components/WorldClock.md) | context | read | tick (`episodeTicks`) | the time limit at the world's rate |
| [Parked](../components/Parked.md) | context | write | tick (kill) | parks the world until the window ends |

## Side effects

- Parking stops `PhysicsStep` and every other non-observe system of the world until the observe phase.

## Order and dependencies

- After `BodySync` (state) and `Task` (`route_lost`); before the observe phase (`Rewards`, `Episode`).

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/humanbot.cpp`: S4.3 (kills and resets).
- `tests/unit/terrain_walker.cpp`: TW.4, TW.6 (a limp ragdoll is killed as on flat ground), TW.7 (height on slopes).
- `tests/unit/humanbot_rig.cpp`: S8.4 (falling under fixed actions against Unity's rollouts).
