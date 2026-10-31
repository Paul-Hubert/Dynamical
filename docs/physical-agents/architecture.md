# Architecture

## Targets

| Target | Kind | Links | Contents |
|---|---|---|---|
| `dynamical_sim` | static lib | EnTT, glm, cereal, nlohmann-json, Taskflow, FastNoise2, Jolt | `logic/` without the graphics and input files, `physics/`, `scene/`, `agent/`, `util/`, header-only `extra/optick`. **No Vulkan, SDL, OpenXR or ImGui headers** |
| `dynamical_mlagents` | static lib | `dynamical_sim`, gRPC, protobuf | Generated protos, gRPC client, side channels, `.demo` writer (from P3) |
| `dynamical_onnx` | static lib | `dynamical_sim`, ONNX Runtime | `PolicyRunner`, `PolicySys`, `ModelWatcher` core (from P11) |
| `dynamical` | exe (window) | `dynamical_sim`, `dynamical_onnx` (when built), Vulkan, SDL2, OpenXR, VMA, ImGui, ImPlot | Renderer, input, UI, panels, `WorldView`, view systems, capture. Rendered scenes |
| `dynamical_train` | exe (console) | `dynamical_sim`, `dynamical_mlagents`, `dynamical_onnx` (when built) | `TrainHost` and its modes. Output name `DYNAMICAL_TRAIN_NAME` (default `TrainingEnv`), because the trainer globs `<path>.exe` (UA §9.4) |
| `dynamical_tests` | exe (console) | `dynamical_sim`, doctest; `dynamical_mlagents` / `dynamical_onnx` **when those targets exist** | Unit, scenario, oracle and protocol tests |
| `imgdiff` | exe (tool) | stb | Image comparison for visual tests (P0) |

Systems register by name in the target that compiles them ([scenes.md](scenes.md#registries)). A scene
that names a system its executable does not link fails to load with a clear error.

## Library boundary

Today `src/logic` mixes simulation with ImGui, SDL, Vulkan and the renderer. What moves where:

| File (master) | Problem (evidence) | Goes to |
|---|---|---|
| `logic/systems/system.h` | includes `vulkan/vulkan.h` (`:8`) and `extra/optick/optick.h` (`:9`) | lib. Remove the Vulkan include. Optick stays header-only, with `USE_OPTICK` undefined; `optick_gpu.vulkan.cpp` is never compiled into the lib |
| `logic/game.cpp`, `game.h` | includes `renderer/renderer.h`, `renderer/terrain/terrain_sys.h` (`game.cpp:7-8`) | `dynamical` (`src/view/game.*`). `dynamical_train` has its own `TrainHost` loop |
| `logic/components/input.h` | includes `<SDL.h>` (`:8`) | `dynamical` |
| `logic/systems/input.*`, `ui.*`, `debug_menu.cpp` | SDL / ImGui | `dynamical` |
| `logic/systems/player_control.cpp`, `vr_control.cpp` | `Input`, `VRInput`, renderer types | `dynamical` |
| `logic/systems/scene.cpp`, `logic/scene_spawn.h` | ImGui, `ModelManager` (`scene.cpp:7,17-18`). `spawnBox` is used by `vr_control.cpp:101` | `dynamical`: `src/view/spawn.*` keeps `spawnBox` for `BoxThrower` and `VRControl`. `SceneSys` becomes the `BoxRing`, `ModelLineup` and `BoxThrower` systems |
| `logic/systems/time.cpp` | ImGui + `Input` (`:6`, `:24`), tile-map game-speed clock; nothing else reads `Time` | `dynamical` |
| `logic/systems/chunk_systems.*`, `logic/terrain/*` | none | lib |
| `physics/*` | none | lib |
| `logic/components/model_renderable.h` | holds a `ModelManager` entity; no default constructor (`:26`) | `dynamical`. The lib uses `ModelRef { path, color }` |
| `renderer/**` (including `terrain_sys`) | Vulkan | `dynamical` |

A CTest test `header_boundary` greps the library sources for `vulkan`, `SDL`, `imgui`, `openxr` and
`renderer/`. Any hit fails it, and it runs in CI.

## Run arguments

`Settings` (`settings.cpp`) knows 9 keys, silently ignores the rest, and **writes `./config.6.json`
on every start** (`settings.cpp:17,23`). N `TrainingEnv.exe` processes in one directory would race on
that file. So:

- **`RunArgs`** (lib, `src/scene/run_args.*`) parses `key=value` and `var.<name>=<value>`. Unknown keys
  are an **error** in `dynamical_train` and the tests, and a warning in `dynamical`.

  | Group | Keys |
  |---|---|
  | Scene | `scene`, `var.*` |
  | Process | `mode`, `threads`, `ticks`, `episodes` |
  | Capture | `capture`, `fixed_frame_dt`, `window_hidden`, `exit_after_capture` |
  | Probes | `probe`, `probe_entities`, `probe_every` |
  | Trainer | `-logFile`, `--mlagents-port`, `-envParams…`, `-set:` (`train_args.cpp`) |
- **`Settings`** stays for `dynamical` only: window, VR and view distance, loaded and saved as today.
  `dynamical_train` and the tests never construct it.
- **Resource lookup:**
  - `dynamical` keeps the uncommitted `find_resources()` (`main.cpp`), which may change the working
    directory;
  - `dynamical_train` and the tests resolve resources from the executable directory
    (`GetModuleFileNameW`) and **never change the working directory**, because the trainer passes
    relative output paths.

## Directories

```
src/
  main.cpp                    dynamical entry
  train_main.cpp              dynamical_train entry
  logic/  physics/  util/     existing, split as above
  scene/          new  lib    scene_def, scene_loader, run_args, world, system_registry, component_registry, prefabs, scenario_runner
  agent/          new  lib    unity_import, body_def, pose_track, ragdoll_builder, components, layouts/, rewards/, systems/
                              (systems include BodySync, TrackActions, RigChecks, so rig checks and demos run headless)
  train/          new  train  train_host, train_args, modes (null, bench, train, eval, record_demo)
  mlagents/       new  mlag   proto/ (vendored, VERSION = fork commit), client, side_channels, bridge, demo_writer
  inference/      new  onnx   policy_runner, policy_sys, model_watcher
  view/           new  exe    game, spawn, world_view, world_stepper, capture, agent_panel, rig_panel, replay_sys
  renderer/                   pass list from the scene; capture readback
resources/
  scenes/         new         *.scene.json (list in scenes.md), common/*.json
  prefabs/        new         box, ground_flat, humanbot
  agents/         new         bodies/, tracks/, params/ (exported from Unity)
  models/                     glTF; `ModelLineup` lines up this folder (non-recursive)
  models/primitives/  new     box.gltf, sphere.gltf (capsules are generated); kept out of the lineup
tests/            new         unit/, scenario/, protocol/, data/, golden/ (images, CHANGELOG.md), capture/*.capture.json
tools/                        imgdiff/, rebaseline.py, phase_report.py, train_smoke.py, plot_*.py, fit_gain_scale.py
```

## CMake

```cmake
option(DYNAMICAL_BUILD_VIEWER "Build dynamical (Vulkan, SDL, OpenXR, shaders)" ON)   # OFF in CI
option(DYNAMICAL_TRAIN "Build the headless training executable" ON)
option(DYNAMICAL_ONNX  "Build ONNX Runtime inference"           OFF)                  # ON from P11
option(DYNAMICAL_TESTS "Build the tests"                         ON)
set(DYNAMICAL_TRAIN_NAME "TrainingEnv" CACHE STRING "Output name of dynamical_train")

# Common flags for every target (master sets them PRIVATE on dynamical only, CMakeLists.txt:28-31)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC) add_compile_options(/bigobj /utf-8) endif()

find_package(EnTT CONFIG REQUIRED)  find_package(Jolt CONFIG REQUIRED)  find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)   find_package(Taskflow CONFIG REQUIRED)
find_path(CEREAL_INCLUDE_DIRS cereal/cereal.hpp)                                      # master uses find_path for cereal/stb

file(GLOB_RECURSE SIM_SOURCES src/logic/*.cpp src/physics/*.cpp src/scene/*.cpp src/agent/*.cpp src/util/*.cpp)
list(FILTER SIM_SOURCES EXCLUDE REGEX "src/logic/(game|systems/(input|ui|debug_menu|player_control|vr_control|time|scene))\\.cpp")
add_library(dynamical_sim STATIC ${SIM_SOURCES})
target_include_directories(dynamical_sim PUBLIC src lib lib/FastNoise2/include ${CEREAL_INCLUDE_DIRS})
target_link_libraries(dynamical_sim PUBLIC EnTT::EnTT Jolt::Jolt glm::glm nlohmann_json::nlohmann_json Taskflow::Taskflow FastNoise)

if(DYNAMICAL_TRAIN)   # P3
  find_package(Protobuf CONFIG REQUIRED)  find_package(gRPC CONFIG REQUIRED)
  add_library(dynamical_mlagents STATIC src/mlagents/client.cpp src/mlagents/side_channels.cpp src/mlagents/bridge.cpp src/mlagents/demo_writer.cpp)
  file(GLOB MLAGENTS_PROTOS src/mlagents/proto/mlagents_envs/communicator_objects/*.proto)
  target_sources(dynamical_mlagents PRIVATE ${MLAGENTS_PROTOS})
  protobuf_generate(TARGET dynamical_mlagents LANGUAGE cpp IMPORT_DIRS src/mlagents/proto PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
  protobuf_generate(TARGET dynamical_mlagents LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
                    PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
                    IMPORT_DIRS src/mlagents/proto PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
  target_include_directories(dynamical_mlagents PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/proto)
  target_link_libraries(dynamical_mlagents PUBLIC dynamical_sim gRPC::grpc++ protobuf::libprotobuf)
  add_executable(dynamical_train src/train_main.cpp ${TRAIN_SOURCES})
  set_target_properties(dynamical_train PROPERTIES OUTPUT_NAME ${DYNAMICAL_TRAIN_NAME})
endif()
```

- **Registration.** `registerSimSystems()` is called explicitly from each `main` and the test main.
  Static self-registration would be dropped by the linker in a static library.
- **protoc.** The protoc and `grpc_cpp_plugin` used by `protobuf_generate` are vcpkg's host tools, so
  their versions match the runtime libraries. The generated files go to the build directory, never into
  git.
- **Resources:**
  - `dynamical` keeps master's copy (`CMakeLists.txt:87-94`);
  - `dynamical_train` gets a post-build copy of `resources/{scenes,prefabs,agents}`;
  - tests run with `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}` (`doctest_discover_tests(... WORKING_DIRECTORY ...)`)
    and read `tests/data` and `resources` in place.
- **Presets.** The VS generator is multi-config, so add **build** and **test** presets
  (`vcpkg-relwithdebinfo`: `configuration: RelWithDebInfo`) on the existing `vcpkg` configure preset.
  Add a `ci` configure preset with `DYNAMICAL_BUILD_VIEWER=OFF`, which needs neither the Vulkan SDK nor
  `glslangValidator`.
- **CI** (`.github/workflows/ci.yml`, Windows): submodules, vcpkg binary cache (GitHub Actions cache
  backend), `ci` preset, `ctest -L "unit|scenario|protocol"`. The first gRPC build takes 30–60 min and
  is then cached. `gpu` tests run on the dev machine, or on a self-hosted runner with a GPU.

### Dependencies to add

| Dependency | vcpkg | Used by | Phase |
|---|---|---|---|
| doctest | `doctest` | tests | P0 |
| Jolt 5.6.0 | overlay port `ports/joltphysics` (patches P-A, P-C); **no baseline bump** | sim | P2, P7 |
| gRPC, protobuf | `grpc`, `protobuf` | `dynamical_mlagents` | P3 |
| ONNX Runtime | `onnxruntime` (*verify* the availability and CPU-only build at the pinned baseline; fallback is the official prebuilt zip through `FetchContent`) | `dynamical_onnx` | P11 |

nlohmann-json, Jolt, Taskflow, EnTT, glm, cereal and stb are already in `vcpkg.json`. Add matching
entries to `flake.nix`.

## Threading

| Process | Thread | Work |
|---|---|---|
| `dynamical_train` | main | `TrainHost`: gRPC exchange (1 s deadline), gather and scatter |
| | Taskflow workers | One task per world: `decision_period` fixed ticks |
| | Jolt | None. Each world uses `JobSystemSingleThreaded` |
| `dynamical` (`base`) | main | Frame loop, host systems, `PhysicsSys` with Jolt's thread pool (as today) |
| `dynamical` (agent scenes) | main | Frame loop; `WorldStepper` steps the few worlds in real time, single-threaded each |

Worker count: `threads=<n>` (default `min(num_worlds, hardware_concurrency)`). When `start-run` launches
many env processes, prefer `threads=1` and more processes.
