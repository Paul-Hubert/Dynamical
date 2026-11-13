# src (root)

The two entry points of the engine. Everything else of `src/` is in its folders: the simulation library
`dynamical_sim` (`logic/`, `physics/`, `scene/`, `util/`, `train/`, `onnx/`), the viewer (`view/`, `renderer/`).

| File | What |
|---|---|
| [`main.cpp`](../../../src/main.cpp) | `dynamical`: profiler on `profile=true`, finds the `resources` folder (moves to the closest parent of the executable that has one), `JoltRuntime`, registers the sim, view and policy systems, runs `Game` |
| [`train_main.cpp`](../../../src/train_main.cpp) | `dynamical_train` (`TrainingEnv.exe`): headless, stop handler, `TrainArgs` + `RunArgs` (unknown keys are errors), log file, `JoltRuntime`, `registerSimSystems`, a `ScenarioRunner` on `scene=` (default `walker_train`, `null` for `mode=null`) from the resources next to the executable, `scene.resolved.json`, `TrainHost::run` |

## Systems registered here

None (they call `registerSimSystems`, `registerViewSystems`, `registerPolicySystems`).

## Components and context defined here

None.

## Main functions

- `find_resources()` (main.cpp): working directory fix for an executable started elsewhere.
- The order every main follows: `JoltRuntime` before any `PhysicsWorld`, systems registered before a scene is
  loaded (the loader checks names against the registry).

## Dependencies

[`src/view`](src-view.md) (`Game`, `registerViewSystems`), [`src/scene`](src-scene.md), [`src/physics`](src-physics.md)
(`JoltRuntime`), [`src/train`](src-train.md), [`src/onnx`](src-onnx.md) (with `DYNAMICAL_ONNX`), [`src/util`](src-util.md).
The page of `Game`, the worlds and the system set: [service-Game](../systems/service-Game.md).

## Design docs

[CLAUDE.md](../../../CLAUDE.md) (build and run), [docs/physical-agents/mlagents-bridge.md](../../physical-agents/mlagents-bridge.md),
[docs/physical-agents/simulation.md](../../physical-agents/simulation.md), [docs/profiling.md](../../profiling.md).
