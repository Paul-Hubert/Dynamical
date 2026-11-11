# `src/`

> [Source](../../../src/) · Parent: none (source root)

## Summary
All C++20 sources of Dynamical. The code splits into a headless simulation library and two front ends. `dynamical_sim` (static library, CMake glob of `logic/`, `physics/`, `scene/`, `util/`) holds the ECS components and systems, Jolt physics, the marching cubes terrain generator, scene loading and the per-world registries (`World` / `Worlds`), and is shared by every executable and the tests. The `dynamical` executable (`main.cpp` + `view/` + `renderer/` + `extra/`) adds the Vulkan / OpenXR renderer, the host frame loop (`Game`) and the interactive host systems. The `dynamical_train` executable (`train_main.cpp`, output name set by `DYNAMICAL_TRAIN_NAME`, e.g. TrainingEnv.exe) runs scenes headless for ML-Agents training through `train/` and `mlagents/`. `onnx/` (option `DYNAMICAL_ONNX`, default on Windows) adds ONNX Runtime policy inference to dynamical. Systems are registered by name (`registerSimSystems()` in `scene/system_registry.cpp`, `registerViewSystems()` in `view/register_view.cpp`, `registerPolicySystems()` in `onnx/`) and instantiated from scene JSON files.

## Files
| File | Contents |
|---|---|
| `main.cpp` | dynamical's entry point: `find_resources()` (if `./resources` is missing, `chdir` to the nearest of up to 4 parents of `SDL_GetBasePath()` holding a `resources` folder); creates `JoltRuntime`; `registerSimSystems()`, `registerViewSystems()`, `registerPolicySystems()` (with `DYNAMICAL_ONNX`); constructs `dy::Game(argc, argv)`, returns `game->start()` after destroying the Game. |
| `train_main.cpp` | dynamical_train's entry point: `installStopHandler()`, `parseTrainArgs`, optional log file (`setLogFile`), `parseRunArgs` on the remaining arguments (unknown keys and errors are fatal, exit 1); `JoltRuntime`, `registerSimSystems()`; resources next to the executable, `allow_renderer = false`; scene = `scene=`, or `null` for `mode=null`, else `walker_train`; builds a `ScenarioRunner` (SceneError -> exit 1); writes `scene.resolved.json` next to the log file; runs `TrainHost(args, train, runner).run()`. Never changes the working directory or touches `config.6.json`. |

## Systems defined here
None directly (see the subfolders).

## Components defined here
None directly (see the subfolders).

## Subfolders
- [`src/logic/`](src-logic.md): simulation core of `dynamical_sim`: components (Transform, RigidBody, Ground, Camera, VRRig / VRInput, RenderSettings, FrameClock, ModelRef), the `System` / `SystemSet` base (lanes Pre / Post / Fixed, phases Every / Act / Observe), chunk streaming and generation systems, BoxDrop, the sphere-roller agent (`agents/`), Settings (`config.6.json`), and the terrain generator and CPU marching cubes (`terrain/`).
- [`src/physics/`](src-physics.md): Jolt integration: `JoltRuntime` (process-wide Jolt state), `PhysicsWorld` (one Jolt PhysicsSystem per registry, ground and static queries), the host `PhysicsSys` / world `PhysicsStepSys`, `RigidBodySys`, height fields and terrain colliders.
- [`src/scene/`](src-scene.md): scene files to running systems: scene loader and `SceneDef`, the system and component registries, `World` / `Worlds` (per-world registry, physics, clock, RNG, fixed-lane SystemSet), `RunArgs` parsing, `ScenarioRunner` (headless stepping for tests and training), `Probe` (CSV pose sampling).
- [`src/renderer/`](src-renderer.md): Vulkan renderer of dynamical: context (OpenXR first, SDL window, device, VMA, transfer, compute, swapchain), desktop and VR render targets, scene passes (terrain, grass, model, UI), glTF models and `ModelManager`, the GPU marching cubes `TerrainSys`, shaders.
- [`src/view/`](src-view.md): dynamical's front end: `Game` (host registry and frame loop), capture scripts, `registerViewSystems`, the world viewer and stepper, sphere-roller viewer and players, and the interactive host systems.
- [`src/onnx/`](src-onnx.md): ONNX Runtime inference (`PolicyRunner`) of exported ML-Agents policies and the host `ModelWatcher` / world `Policy` systems (`registerPolicySystems`); compiled only with `DYNAMICAL_ONNX`.
- [`src/train/`](src-train.md): dynamical_train's host: `TrainHost` loop, training arguments and process control (stop handler), environment parameters, the `Environment` interface with `WorldsEnv` (agent worlds) and `NullEnv` (trainer ceiling test).
- [`src/mlagents/`](src-mlagents.md): ML-Agents protocol 1.5.0 client over gRPC (engine side of `UnityToExternalProto.Exchange`), side channels, generated protobuf sources.
- [`src/util/`](src-util.md): shared helpers: logging, paths (`executableDirectory`), primitive meshes (capsule, sphere), EnTT helpers, `ThreadSafe<T>` and misc utilities, profiling macros, noise and quad-tree headers.
- [`src/extra/`](src-extra.md): vendored Optick profiler (`extra/optick/`), compiled into dynamical; `OPTICK_*` macros (app registration currently commented out in `main.cpp`).
