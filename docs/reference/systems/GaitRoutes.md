# GaitRoutes

`GaitRoutesSys` in [`src/logic/agents/walk_routes.cpp`](../../../src/logic/agents/walk_routes.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, before `GaitBase` (template `common/ik_demo_world.json`) · Parameters: yes

The recording episodes of the kinematic walk (scenes `walker_demo_gait`, `walker_demo_terrain`;
docs/physical-agents/reports/terrain-walker.md): the GAIL demonstrations of the physical walker. Each episode walks a
fresh route drawn by the rig entity's [`WalkRoutes`](../components/WalkRoutes.md) in the slope band, from a stand; the
first `warmup` (+ up to `jitter`) seconds are not recorded, then `decisions` decisions are marked recordable, the
last one done.

## Behaviour

- **constructor**: reads `warmup`, `jitter` (at least 0) and `decisions` (at least 1).
- **tick**: returns until the world's [`HumanBot`](../components/HumanBot.md) is built (`AgentFactory`). Gets the rig
  entity (`ensureRig`: the entity with [`HumanBotSpawn`](../components/HumanBotSpawn.md), given
  [`Rig`](../components/Rig.md) and [`RigPose`](../components/RigPose.md) at the spawn pose on the first call) and its
  [`GaitRoute`](../components/GaitRoute.md) (emplaced).
  - **Start of a route** on the first tick, and on the first tick of a window once `warm_ticks + decisions *
    decision_period` ticks have passed since the last start (`start`):
    - `warm_ticks` = `warmup + jitter * uniform` seconds, rounded up to whole windows.
    - Copies the entity's `WalkRoutes` (defaults without one), sets its `length` to the distance walked in the whole
      episode at the commanded speed + 1.5 m, and draws a start with `drawStart` on the world's
      [`GroundField`](../components/GroundField.md): points within `spawn_radius` of `centre` where the ground is level
      enough within `clear` of the point, and a direction whose path (`measureRoute`, the path and `half_width` to each
      side every 0.5 m) stays within `up`, `down` and `across`. When none qualifies after `spawn_tries` points it logs a
      warning and walks from the last point tried.
    - Writes the route ([`GaitCommand`](../components/GaitCommand.md)`::start` = the point, `heading` = the direction's
      yaw; the speed is left as it is).
    - Starts the gait over: destroys the legs' [`IKChain`](../components/IKChain.md) and
      [`IKTarget`](../components/IKTarget.md) entities named by the old [`GaitState`](../components/GaitState.md),
      replaces `GaitState` with a fresh one (so `GaitBase` starts again, standing, and teleports the bodies), removes
      [`BalanceState`](../components/BalanceState.md), clears [`GaitReport`](../components/GaitReport.md)`::is_planted`
      and its `settle_time`, and marks the bot's observation history invalid (`HumanBot::state.history_valid`).
    - Records `start_tick` and counts the episode.
  - **Every tick**: `AgentIO::recordable = false`, `done = truncated = false`. On the last tick of a window
    (`tick % period == period - 1`) once the warm-up is over: `recordable = true`; on the first recorded decision it
    zeroes `HumanBot::previous_frame` (the older half of the observation stack starts empty, as in every trainer
    episode); on the `decisions`-th, `done = truncated = true`.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `warmup` | float | 3.0 | seconds walked on each route before it is recorded (the start and two strides) |
| `jitter` | float | 1.2 | up to this many more seconds, drawn per route, so that recordings start anywhere in the stride |
| `decisions` | int | 150 | recorded decisions per route |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick, start | `built` read; `previous_frame` zeroed, `state.history_valid` cleared |
| [HumanBotSpawn](../components/HumanBotSpawn.md) | component | read | ensureRig | finds the rig entity |
| [Rig](../components/Rig.md) | component | write+create | ensureRig | created on the first call with the bot's body |
| [RigPose](../components/RigPose.md) | component | write+create | ensureRig | the spawn pose, on the first call |
| [GaitRoute](../components/GaitRoute.md) | component | readwrite+create | tick, start | the route and its timing |
| [WalkRoutes](../components/WalkRoutes.md) | component | read | start | the slope band and the spawn area |
| [GaitCommand](../components/GaitCommand.md) | component | readwrite+create | start | speed read (path length); `start` and `heading` written |
| [GaitState](../components/GaitState.md) | component | write+create | start | replaced by a fresh state |
| [IKChain](../components/IKChain.md) | component | destroy | start | the legs' chain entities of the old gait destroyed |
| [IKTarget](../components/IKTarget.md) | component | destroy | start | the legs' target entities destroyed |
| [BalanceState](../components/BalanceState.md) | component | destroy | start | removed: the balance starts over |
| [GaitReport](../components/GaitReport.md) | component | readwrite | start | planted points and settle time cleared (when present) |
| [GroundField](../components/GroundField.md) | context | read+create | start | where routes are drawn (a level plane is made when missing) |
| [AgentIO](../components/AgentIO.md) | context | readwrite+create | tick | `recordable`, `done`, `truncated` |
| [WorldClock](../components/WorldClock.md) | context | read | tick, start | ticks, windows, dt |
| [WorldRng](../components/WorldRng.md) | context | readwrite | start | jitter, spawn points and directions |

## Side effects

- Entity destruction: the old legs' IK chain and target entities at each new route. The arms' chain and target
  entities (made by `GaitHandIK`, which `ik_demo_world` does not run) are not destroyed: a fresh `GaitState` forgets them.
- Log: a warning when no route in the band is found.

## Order and dependencies

- After `AgentFactory` (the `HumanBot` and the spawn entity) and the ground system (`GroundPlane` / `GroundTerrain`);
  first of the gait, before `GaitBase`, which reads the new command and starts from `GaitCommand::start`.
- `Observation@observe` records the decisions marked `recordable`; the demonstration writer reads `done`.

## Used in scenes

<!-- generated -->
- [`common/ik_demo_world.json`](../../../resources/scenes/common/ik_demo_world.json): `fixed`

## Tests

- `TW.9`, `TW.10` in [`tests/unit/terrain_walker.cpp`](../../../tests/unit/terrain_walker.cpp) (the gait demos); `TW.1` to `TW.3` cover `measureRoute`, `drawDirection` and `drawStart`
