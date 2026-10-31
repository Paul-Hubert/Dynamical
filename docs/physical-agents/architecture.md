# Architecture

## Targets

| Target | Kind | Links | Contents |
|---|---|---|---|
| `dynamical_sim` | static lib | EnTT, glm, cereal, nlohmann-json, Taskflow, FastNoise2, Jolt | `logic/` without the graphics and input files, `physics/`, `scene/`, `agent/`, `util/`. **No Vulkan, SDL, OpenXR or ImGui headers** |
| `dynamical_mlagents` | static lib | `dynamical_sim`, gRPC, protobuf | Generated protos, gRPC client, side channels, `.demo` writer |
| `dynamical_onnx` | static lib | `dynamical_sim`, ONNX Runtime | `PolicyRunner`, `PolicySys` |
| `dynamical` | exe (window) | `dynamical_sim`, `dynamical_onnx`, Vulkan, SDL2, OpenXR, VMA, ImGui, ImPlot | Renderer, input, UI, panels, `WorldView`, game-only systems. Scenes `base`, `rig_anim`, `walker_view` |
| `dynamical_train` | exe (console) | `dynamical_sim`, `dynamical_mlagents`, `dynamical_onnx` | `TrainHost` and its modes. Scene `walker_train`. Output name configurable (`DYNAMICAL_TRAIN_NAME`, default `TrainingEnv`) because the trainer globs `<path>.exe` (UA §9.4) |
| `dynamical_tests` | exe (console) | `dynamical_sim`, `dynamical_mlagents`, `dynamical_onnx`, doctest | Unit, physics, oracle and protocol tests |

Systems register by name in the target that compiles them (see [scenes.md](scenes.md#registries)).
A scene that names a system its executable does not link fails to load with a clear error. For
example, `walker_view` cannot load in `dynamical_train`.

## Library boundary

Today `src/logic` mixes simulation with ImGui, SDL and Vulkan. What moves where:

| File (master) | Problem | Goes to |
|---|---|---|
| `logic/systems/system.h` | includes `vulkan/vulkan.h` | lib, include removed |
| `logic/systems/input.*`, `ui.*`, `debug_menu.cpp` | SDL / ImGui | `dynamical` |
| `logic/systems/player_control.cpp`, `vr_control.cpp` | `Input`, `VRInput`, renderer types | `dynamical` |
| `logic/systems/scene.cpp` (`SceneSys`, `spawnBox`) | ImGui, `ModelManager` | The spawning moves into the scene loader and prefabs. Box throwing becomes `BoxThrowerSys` in `dynamical` |
| `logic/systems/chunk_systems.*`, `logic/terrain/*` | none | lib |
| `logic/systems/time.cpp` | check for ImGui use | lib if clean, otherwise split |
| `physics/*` | none | lib |
| `renderer/**` (including `terrain_sys`) | Vulkan | `dynamical` |
| `logic/components/model_renderable.h` | holds an entity of `ModelManager` (renderer) | lib: a plain component; only `dynamical` fills it |

`tests/header_boundary` greps the library sources for `vulkan`, `SDL`, `imgui`, `openxr` and
`renderer/`; any hit fails the build.

## Directories

```
src/
  main.cpp                    dynamical entry
  train_main.cpp              dynamical_train entry
  logic/  physics/  util/     existing, split as above
  scene/          new  lib    scene_def, scene_loader, variables, system_registry, component_registry, prefabs
  agent/          new  lib    body_def, pose_track, ragdoll_builder, components, layouts/, rewards/, systems/
  train/          new  train  train_host, train_args, modes (null, bench, train, eval, record_demo)
  mlagents/       new  mlag   proto/ (vendored from the fork), client, side_channels, bridge, demo_writer
  inference/      new  onnx   policy_runner, policy_sys, model_watcher
  view/           new  exe    world_view, agent_panels, rig_anim_sys, replay_sys
  renderer/                   unchanged apart from the optional pass list
resources/
  scenes/         new         base, rig_anim, walker_train, walker_view, common/*.json
  prefabs/        new         box, ground_flat, humanbot
  agents/         new         bodies/, tracks/, params/ (exported from Unity)
  models/                     glTF (box.gltf exists; add capsule.gltf, sphere.gltf)
tests/            new
```

## CMake

```cmake
option(DYNAMICAL_TRAIN "Build the headless training executable" ON)
option(DYNAMICAL_ONNX  "Build ONNX Runtime inference"           ON)
option(DYNAMICAL_TESTS "Build the tests"                         ON)
set(DYNAMICAL_TRAIN_NAME "TrainingEnv" CACHE STRING "Output name of dynamical_train")

file(GLOB_RECURSE SIM_SOURCES src/logic/*.cpp src/physics/*.cpp src/scene/*.cpp src/agent/*.cpp src/util/*.cpp)
list(FILTER SIM_SOURCES EXCLUDE REGEX "src/logic/systems/(input|ui|debug_menu|player_control|vr_control)\\.cpp")
add_library(dynamical_sim STATIC ${SIM_SOURCES})
target_link_libraries(dynamical_sim PUBLIC EnTT::EnTT Jolt::Jolt glm::glm nlohmann_json::nlohmann_json Taskflow::Taskflow FastNoise)

# dynamical: the remaining sources plus src/view, renderer, shaders (current setup)
# dynamical_train: src/train_main.cpp src/train/*.cpp, links dynamical_mlagents dynamical_onnx,
#                  set_target_properties(dynamical_train PROPERTIES OUTPUT_NAME ${DYNAMICAL_TRAIN_NAME})
```

- Static registration through `DY_REGISTER_SYSTEM` lives in static libraries, so the linker may drop
  it. Either link `dynamical_sim` with `/WHOLEARCHIVE` (MSVC) or `--whole-archive` (GCC/Clang), or call
  an explicit `registerSimSystems()` from each `main`. **Use the explicit call**: it is portable and
  makes the system list visible.
- Shaders and resource copying stay attached to `dynamical`. `dynamical_train` gets a post-build copy
  of `resources/scenes`, `resources/prefabs` and `resources/agents` only.
- Presets: keep `vcpkg`, and add `vcpkg-release` (RelWithDebInfo). Training speed depends on an
  optimised Jolt.

### Dependencies to add

| Dependency | vcpkg | Used by |
|---|---|---|
| gRPC, protobuf | `grpc`, `protobuf` | `dynamical_mlagents` (train, tests). The first build is long; use the binary cache |
| ONNX Runtime | `onnxruntime` | `dynamical_onnx`. CPU provider |
| doctest | `doctest` | tests |

nlohmann-json, Jolt, Taskflow and EnTT are already in `vcpkg.json`. Add matching entries to
`flake.nix`.

## Threading

| Process | Thread | Work |
|---|---|---|
| `dynamical_train` | main | `TrainHost`: blocking gRPC exchange, gather and scatter |
| | Taskflow workers | One task per world: `decision_period` fixed ticks |
| | Jolt | None. Each world uses `JobSystemSingleThreaded` |
| `dynamical` (`base`) | main | Frame loop, host systems, `PhysicsSys` with Jolt's thread pool (as today) |
| `dynamical` (`rig_anim`, `walker_view`) | main | Frame loop; `WorldStepper` steps the few worlds in real time, single-threaded each |

Worker count: `threads=<n>` (default `min(num_worlds, hardware_concurrency)`). When `start-run` launches
many env processes, prefer `threads=1` and more processes.
