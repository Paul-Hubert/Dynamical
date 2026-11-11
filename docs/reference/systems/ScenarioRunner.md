# ScenarioRunner

> **Kind:** service · **Registry name:** — · **Scope:** n/a · **Lane:** called by `TrainHost` / `WorldsEnvironment` and tests · **Needs renderer:** no
> **Source:** [`src/scene/scenario_runner.h`](../../../src/scene/scenario_runner.h), [`src/scene/scenario_runner.cpp`](../../../src/scene/scenario_runner.cpp)

## Purpose
Headless scene driver used by `dynamical_train` and the test suites: loads a scene, builds its worlds and steps them without a renderer or host systems. The scene's `renderer` and `host` blocks are ignored (the loader still validates them according to the options), so a world template that only uses library systems runs unchanged.

## Lifecycle
- **Construction / params:** `ScenarioRunner(scene, vars, options, base_seed = 0)`: `def = loadScene(scene, vars, options)` (throws `SceneError`), then `worlds.create(def, base_seed)`, `worlds.preinit()`, `worlds.init()`.
- **preinit:** done in the constructor and in `restart`.
- **init:** done in the constructor and in `restart`.
- **tick(dt):** `step(ticks, executor = nullptr)`: `worlds.step(ticks, executor)`, then `ticks_done += ticks`. `restart(base_seed)`: `worlds.finish()`, `worlds.list.clear()`, `create(def, base_seed)`, `preinit`, `init`, `ticks_done = 0` (the scene is not reloaded).
- **finish:** destructor calls `worlds.finish()`.

## Expected behavior
- `scene()` returns the `SceneDef` loaded once; `worldCount()` and `world(i)` expose the worlds.
- `ticks()` counts ticks stepped since construction or the last `restart`.
- After `restart(s)` the worlds equal a fresh runner built with base seed `s` (TrainHost relies on this: the trainer's seed arrives after the scene was checked).
- References to world registries or their context variables (e.g. `AgentIO*`) are invalidated by `restart`.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| None directly | — | — | — | everything goes through [Worlds](Worlds.md) / [World](World.md) |

## Side effects
- Reads scene, prefab and referenced JSON files through the loader.
- Creates and destroys worlds (Jolt physics systems); `step` with an executor runs taskflow tasks.

## Ordering and dependencies
- Needs a live `JoltRuntime` and the registries filled (`registerSimSystems()`, plus `registerPolicySystems()` for templates using `Policy` / `ModelWatcher`) before construction.
- Not thread safe; one caller at a time.

## Related
[World](World.md), [Worlds](Worlds.md), [SceneLoader](SceneLoader.md), [TrainHost](TrainHost.md), [WorldsEnvironment](WorldsEnvironment.md), [folder `src/scene/`](../folders/src-scene.md).
