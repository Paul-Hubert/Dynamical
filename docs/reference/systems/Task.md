# Task

`HumanBotTaskSys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after `BodySync` · Parameters: no

The HumanBot walker's task (Unity's `ArcUpdate`): every `arcFrames` the target is placed again (and, out of the
continuous speed mode, a new target speed drawn), so the walker is sent somewhere else during an episode.

## Behaviour

**tick** (every tick, not while parked). Returns when the bot is not built or recording. `arc_ticks++`; when it
reaches `arcTicks` (`arc_frames_60hz` converted to the world's rate: `round(arcFrames x (1/dt) / 60)`, 400 frames at
60 Hz by default = 6.7 s):

1. `arc_ticks = 0`; the open eval leg is closed (`closeLeg`, eval only: `view_truncate`).
2. `placeTarget`: on a terrain with routes, a new direction from where the spine stands whose path stays in the slope
   band (`drawDirection`, [`walk_routes.cpp`](../../../src/logic/agents/walk_routes.cpp)), target 1000 m along it;
   none found sets `route_lost` (the episode is truncated by `Termination`). On flat ground, a random target 20 to
   30 m from the head when randomising with `randomTarget`, else 1000 m down -z.
3. `drawSpeed` unless in continuous speed mode: one of `speeds` uniformly when randomising with `randomSpeed`.
4. A new eval leg opened (`openLeg`).

The decision snapshot keeps the old command until the next observe phase: the window is rewarded against what the
policy saw.

## Parameters

None; `arcFrames`, `randomTarget`, `randomSpeed`, `speeds`, `continuousSpeed` from the params file / env params.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick | `arc_ticks`, `target`, `target_speed`, `route_lost`, `legs` |
| [WorldClock](../components/WorldClock.md) | context | read | tick (`arcTicks`) | `dt` to convert 60 Hz frames |
| [WorldRng](../components/WorldRng.md) | context | readwrite | tick (`placeTarget`, `drawSpeed`, `drawDirection`) | target, direction and speed draws |

## Side effects

- None outside the context variables (`GroundField` queries through `HumanBot::ground`).

## Order and dependencies

- After `BodySync` (the head and spine positions of the step), before `Termination` (which truncates on
  `route_lost`).

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/terrain_walker.cpp`: TW.2 (directions in the band), TW.11 (eval keeps one stretch per target).
- `tests/unit/humanbot.cpp`: S4.3.
