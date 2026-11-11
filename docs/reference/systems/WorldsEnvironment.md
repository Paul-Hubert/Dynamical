# WorldsEnvironment

> **Kind:** service · **Registry name:** — · **Scope:** World · **Lane:** per trainer message from `TrainHost::serve` · **Needs renderer:** no
> **Source:** [`src/train/worlds_env.h`](../../../src/train/worlds_env.h), [`src/train/worlds_env.cpp`](../../../src/train/worlds_env.cpp), [`src/train/environment.h`](../../../src/train/environment.h)

## Purpose
The `TrainerEnvironment` of `mode=train`: one agent per world, exposed through that world's `AgentIO` context variable, all of one behaviour; agent id = world index. RESET produces fresh observations without stepping, STEP writes the actions and advances every world by one decision window (in parallel on its own executor), and `write` packs every agent's observation, reward and termination into the trainer's output, plus averaged stats on the stats side channel and the brain parameters once.

## Lifecycle
- **Construction / params:** `WorldsEnvironment(ScenarioRunner& runner, unsigned threads)`: a private `tf::Executor(threads)` only when `threads > 1`; stores a pointer to every world's `AgentIO` (`ctx().get`); `done` all 0; `period` = world 0's `WorldClock.decision_period`. `static check(runner)` returns an error string when there are no worlds, a world has no `AgentIO` or an empty behaviour, or behaviour / `observation_size` / `action_size` / `decision_period` differ from world 0.
- **preinit:** None.
- **init:** None.
- **tick(dt):**
  - `reset()`: for each world, `AgentIO.reset_requested = true`, then `systems.fixed_observe_only()` (Observe-phase systems tick with h = 0); `done` cleared.
  - `step(input, error)`: `requested` = agents not done in the last output. The action list for `behavior()` must hold exactly `requested` entries, else false. For each agent in order: done → `action` filled with zeros; otherwise the next action, which must have exactly `action_size` continuous values (else false), copied into `AgentIO.action`. Then `runner.step(period, executor)`; true.
  - `write(output)`: one `AgentInfoProto` per agent under `behavior()`: `id` = index, `reward`, `done`, `max_step_reached = done && truncated`, group 0 / reward 0, one observation (shape `[observation_size]`, uncompressed float data, dimension property 0, type DEFAULT, name `sensor_name`). Records `done[i]`. Sums every `stats` entry per key across agents; if any, writes one stats side-channel message per key with the mean and `Average` aggregation. On the first call adds brain parameters (name, training, `action_size` continuous, 0 discrete).
- **finish:** destructor (default) destroys the executor.

## Expected behavior
- A done agent receives no action from the trainer; its world still steps the next window with zero actions and begins its next episode (the world's own systems handle the restart).
- STEP with the wrong number of agents or action sizes fails with a descriptive error and does not step.
- RESET does not advance `WorldClock.tick`.
- Brain parameters are sent exactly once.
- MockTrainer run on `sphere_roll`: RESET then 60 STEP then QUIT (`tests/protocol/sphere_env.cpp` SR.9).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | world | read | check | existence, behaviour, sizes; pointer kept |
| [WorldClock](../components/WorldClock.md) | world | read | check | `decision_period` |
| [AgentIO](../components/AgentIO.md) | world | write | reset | `reset_requested = true` |
| [AgentIO](../components/AgentIO.md) | world | write | step | `action` |
| [AgentIO](../components/AgentIO.md) | world | read | write | observation, reward, done, truncated, sensor_name, stats, sizes |

## Side effects
- Ticks the Observe systems of every world on RESET and steps all worlds on STEP (taskflow tasks when `threads > 1`).
- Writes stats side-channel messages into the output proto.
- Does not clear `AgentIO.stats` (left to the world's systems).

## Ordering and dependencies
- Must be built after `ScenarioRunner::restart`, since it caches `AgentIO*` into the current worlds (`TrainHost::agents` does so).
- The action write in `step` happens on the calling thread before the worlds step; outputs are read after the step completes, so no concurrent access to `AgentIO`.

## Related
[TrainHost](TrainHost.md), [ScenarioRunner](ScenarioRunner.md), [World](World.md), [folder `src/train/`](../folders/src-train.md), `docs/physical-agents/mlagents-bridge.md`.
