# Architecture

## Targets

| Target | Kind | Links | Purpose |
|---|---|---|---|
| `dynamical_sim` | static lib | EnTT (vendored), glm, cereal, Taskflow, FastNoise2, Jolt | Map, components, factories, settings, AI, physics, agent runtime. **No Vulkan, SDL or ImGui headers** |
| `dynamical_mlagents` | static lib | `dynamical_sim`, gRPC, protobuf | Generated ML-Agents protos, `MlAgentsClient`, side channels, `.demo` writer |
| `dynamical_onnx` | static lib | `dynamical_sim`, ONNX Runtime | `PolicyRunner` (optional; `-DDYNAMICAL_ONNX=ON`) |
| `dynamical` | exe | `dynamical_sim`, Vulkan, SDL2, VMA, ImGui, ImPlot, optionally `dynamical_onnx` | The game, the agent viewer, replay and video capture |
| `dynamical_train` | exe | `dynamical_sim`, `dynamical_mlagents`, optionally `dynamical_onnx` | Headless training/eval/demo-recording process launched by the trainer |
| `dynamical_tests` | exe | `dynamical_sim`, `dynamical_mlagents`, doctest | Unit, parity and protocol tests (see [testing.md](testing.md)) |

Only `dynamical` and `dynamical_train` are shipped. Both are written to the project root like today.

## Directory layout

New directories are marked **new**; existing ones move only where noted.

```
src/
  main.cpp                     game entry (unchanged)
  logic/                       -> dynamical_sim, except the files listed under "game-only"
    components/  factories/  map/  settings/  systems/
  ai/                          -> dynamical_sim
  util/                        -> dynamical_sim
  physics/        new          -> dynamical_sim
    jolt_init.h/.cpp             global Jolt registration (once per process)
    physics_world.h/.cpp         one JPH::PhysicsSystem + allocators + layers (ctx object)
    layers.h                     object and broad-phase layers
    components.h                 PhysicsBody, BodySegment, JointRef, ArticulatedBody, Ground
    ragdoll_builder.h/.cpp       body file -> JPH::RagdollSettings -> entities
    systems/                     PhysicsStepSys, BodySyncSys, JointDriveSys, ContactSys, EnergySys
  agent/          new          -> dynamical_sim
    components.h                 Agent, AgentId, ActionBuffer, ObservationBuffer, RewardAccumulator, Episode
    body_def.h  scenario.h       cereal-serialised definitions
    observation/  reward/  termination/  randomisation/
    systems/                     ActionApplySys, ObservationSys, RewardSys, TerminationSys, EpisodeSys, StatsSys
    arena.h/.cpp                 Arena, ArenaSet
    behavior_spec.h              behaviour name, observation specs, action spec
  train/          new          -> dynamical_train
    train_main.cpp  train_host.h/.cpp  train_args.h/.cpp
  mlagents/       new          -> dynamical_mlagents
    proto/                       vendored .proto files from the fork (generated .pb.* go to the build dir)
    client.h/.cpp                gRPC Exchange wrapper
    side_channels.h/.cpp         framing, EngineConfiguration, EnvironmentParameters, Stats
    bridge.h/.cpp                AgentInfo gather / action scatter
    demo_writer.h/.cpp
  inference/      new          -> dynamical_onnx
    policy_runner.h/.cpp
  renderer/                    -> dynamical (unchanged)
  logic/systems/agent_view/ new -> dynamical: BodyRenderSys, AgentPanelSys, ReplaySys, CaptureSys
tests/            new          -> dynamical_tests
resources/agents/ new          body files, scenario files, pose tracks, ONNX models
```

### Game-only files (stay in the `dynamical` exe)

These currently live under `src/logic` but touch Vulkan, SDL, ImGui or `Input`, so they are excluded
from the library by an explicit list in CMake:

| File | Reason | Change needed |
|---|---|---|
| `systems/input.*`, `systems/ui.*`, `systems/ui_render.*`, `systems/selection.*` | SDL / ImGui | none, stays in exe |
| `systems/map_upload.*`, `map_render.*`, `object_render.*`, `particle_simulation.*` | Vulkan | none, stays in exe |
| `systems/camera.cpp` | needs `Context*` | stays in exe |
| `systems/devmenu.cpp`, `map_configurator.cpp`, `map_editor.cpp`, `action_bar.cpp` | ImGui | stays in exe |
| `systems/time.cpp` | ImGui panel and `Input` mixed with the clock | **split**: `TimeSys` (clock, lib) + `TimePanelSys` (UI, exe) |
| `map/map_manager.cpp` `getMousePosition` | reads `Input`, `Camera` | move to a free function in the exe |
| `systems/system.h` | includes `vulkan/vulkan.h` | **remove the include**; add it to the render systems that need it |
| `systems/system_list.h` | declares Camera/DevMenu/etc. with lib systems | split into `system_list.h` (lib) and `game_system_list.h` (exe) |

The rule after the split: nothing under `src/logic`, `src/ai`, `src/physics` or `src/agent` may
include `renderer/`, `imgui`, `SDL` or `vulkan`. A CI grep enforces it (see [testing.md](testing.md)).

## CMake

Replace the single `file(GLOB_RECURSE …)` with per-target lists.

```cmake
cmake_minimum_required(VERSION 3.20)
project(dynamical CXX)
set(CMAKE_CXX_STANDARD 20)

option(DYNAMICAL_TRAIN "Build the headless training executable" ON)
option(DYNAMICAL_ONNX  "Build ONNX Runtime inference"          ON)
option(DYNAMICAL_TESTS "Build tests"                            ON)

find_package(Jolt CONFIG REQUIRED)              # vcpkg port: joltphysics

file(GLOB_RECURSE SIM_SOURCES
     src/logic/*.cpp src/ai/*.cpp src/util/*.cpp src/physics/*.cpp src/agent/*.cpp)
list(FILTER SIM_SOURCES EXCLUDE REGEX
     "src/logic/systems/(input|ui|ui_render|selection|map_upload|map_render|object_render|particle_simulation|camera|devmenu|map_configurator|map_editor|action_bar|time_panel)\\.cpp|src/logic/systems/agent_view/")
add_library(dynamical_sim STATIC ${SIM_SOURCES})
target_include_directories(dynamical_sim PUBLIC src lib lib/FastNoise2/include)
target_link_libraries(dynamical_sim PUBLIC Jolt::Jolt FastNoise)

if(DYNAMICAL_TRAIN)
  find_package(Protobuf CONFIG REQUIRED)
  find_package(gRPC CONFIG REQUIRED)
  file(GLOB MLAGENTS_PROTOS src/mlagents/proto/mlagents_envs/communicator_objects/*.proto)
  add_library(dynamical_mlagents STATIC src/mlagents/client.cpp src/mlagents/side_channels.cpp
              src/mlagents/bridge.cpp src/mlagents/demo_writer.cpp ${MLAGENTS_PROTOS})
  target_link_libraries(dynamical_mlagents PUBLIC dynamical_sim gRPC::grpc++ protobuf::libprotobuf)
  target_include_directories(dynamical_mlagents PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/proto)
  protobuf_generate(TARGET dynamical_mlagents LANGUAGE cpp
                    IMPORT_DIRS src/mlagents/proto PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
  protobuf_generate(TARGET dynamical_mlagents LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
                    PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
                    IMPORT_DIRS src/mlagents/proto PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)

  file(GLOB_RECURSE TRAIN_SOURCES src/train/*.cpp)
  add_executable(dynamical_train ${TRAIN_SOURCES})
  target_link_libraries(dynamical_train PRIVATE dynamical_mlagents)
endif()

# the existing game target keeps its Vulkan/SDL/ImGui setup, minus the sim sources
add_executable(dynamical src/main.cpp ${GAME_SOURCES})
target_link_libraries(dynamical PRIVATE dynamical_sim Vulkan::Vulkan ${SDL2_LIBRARIES} VulkanMemoryAllocator)
```

`dynamical_train` is a console app on Windows (no `WIN32` subsystem) so the trainer can capture its
stdout/stderr.

### Dependencies to add

| Dependency | vcpkg (`vcpkg.json`) | Nix (`flake.nix`) | Notes |
|---|---|---|---|
| Jolt Physics ≥ 5.0 | `joltphysics` | `jolt-physics` | Build with `DOUBLE_PRECISION=OFF`, `CROSS_PLATFORM_DETERMINISTIC=ON` (see [physics.md](physics.md#determinism)). The vcpkg port's defaults must match the compile definitions the consumer uses; `Jolt::Jolt` propagates them |
| gRPC + protobuf | `grpc`, `protobuf` | `grpc`, `protobuf` | Only for `dynamical_train`/tests. First vcpkg build is long (~30–60 min); cache the binary |
| ONNX Runtime | `onnxruntime` | `onnxruntime` | CPU provider is enough for 30 Hz MLP policies |
| doctest | `doctest` | `doctest` | Tests |

Also fix the existing mismatch: `CMakePresets.json` names its preset `vcpkg`, while CLAUDE.md says
`cmake --preset default`. Rename the preset to `default` and add a `train-release` preset with
`CMAKE_BUILD_TYPE=Release` and `DYNAMICAL_TRAIN=ON` (training speed depends on optimised Jolt).

## Runtime structure

### Arenas

An **arena** is one independent copy of a training scenario (ML-Agents' "training area").

```cpp
// src/agent/arena.h
struct Arena {
    uint32_t index;                       // 0..num_areas-1
    entt::registry reg;                   // owns all entities of this arena
    std::unique_ptr<SystemSet> fixed;     // systems bound to `reg`, stepped every physics tick
    // reg.ctx holds: PhysicsWorld, ScenarioDef, ArenaRng, ArenaClock, BodyDef*, BehaviorRegistry*
};

struct ArenaSet {                          // stored in the host registry's ctx
    std::vector<std::unique_ptr<Arena>> arenas;
    void step_parallel(tf::Executor&, double h);   // one taskflow for_each over arenas
};
```

Why one registry per arena:

- Episode resets create and destroy entities. EnTT 3.8.1 registries are not thread-safe for
  structural changes, so arenas sharing a registry could not reset in parallel.
- The existing `System(entt::registry&)` pattern then works unchanged: each arena builds its own
  `SystemSet` bound to its own registry.
- Arenas never interact physically, so one `PhysicsSystem` each costs nothing in collision terms and
  keeps every arena deterministic on its own.

The **host registry** (the existing `Game::registry` in the game, a `TrainHost` registry in training)
holds process-wide state: `Settings`, `tf::Executor`, `ArenaSet`, the `MlAgentsBridge`, `BehaviorRegistry`
and the shared immutable `BodyDef`s.

In the game there is a single world, so physical walkers live in the game registry and its ctx holds a
`PhysicsWorld` directly; the same physics and agent systems run there. See
[game-integration.md](game-integration.md).

### Threading

| Thread | Work |
|---|---|
| Main | Host loop, bridge exchange (blocking gRPC call), observation gather and action scatter |
| Taskflow workers | `ArenaSet::step_parallel`: one task per arena, each running `decision_period` physics ticks |
| Jolt | No thread pool. Each arena steps with `JPH::JobSystemSingleThreaded` |

Running Jolt's own `JobSystemThreadPool` next to Taskflow would oversubscribe the CPU. With 6–20 envs
per trainer and N arenas per env, parallelism across arenas is plentiful. In the game (one world, few
agents) use a `JobSystemThreadPool` sized to `hardware_concurrency()/2` instead, or implement
`JPH::JobSystemWithBarrier` on Taskflow later if profiling asks for it.

Worker count: `--threads` flag, default `min(num_areas, hardware_concurrency())`. When the trainer
launches many env processes, it should pass `--threads 1` and use more processes instead.

### Data flow per decision step (training)

```
Python trainer (gRPC server)
      ^  UnityMessageProto{unity_output.rl_output}          |  UnityMessageProto{unity_input.rl_input}
      |                                                     v
 MlAgentsBridge (main thread) ── gather AgentInfo from every arena ── scatter actions into ActionBuffer
      ^                                                     |
      |               ArenaSet::step_parallel(h) x decision_period   (Taskflow, one task per arena)
      |                                                     v
 arena.fixed->tick(h): ActionApply → JointDrive → PhysicsStep → BodySync → Contact → Energy → Reward → Termination
                       (on the last tick of the period) → Observation → Episode
```
