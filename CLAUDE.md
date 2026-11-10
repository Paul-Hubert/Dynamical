# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Dynamical is a C++20 Vulkan engine with a marching cubes voxel world (GPU meshing), grass looked up in baked rays, glTF models, Jolt physics, and OpenXR VR rendering alongside the desktop window. It uses an ECS architecture powered by EnTT.

The previous 2D tile map simulation (tile rendering, heightmap world, SPH particles, AI) is archived in `archive/tilemap/` (not compiled).

## Build System

Uses CMake with vcpkg for dependency management. Generator is Visual Studio 17 2022.

```bash
git submodule update --init   # FastNoise2, VulkanMemoryAllocator, imgui, implot

# Configure (vcpkg toolchain is set via CMakePresets.json, needs VCPKG_ROOT)
cmake --preset vcpkg

# Build
cmake --build build --config Debug

# Run from build/ : resources are loaded from ./resources
cd build && ./Debug/dynamical.exe fullscreen=false window_width=1280 window_height=720 vr_mode=1
```

Shaders (`.glsl` in `src/renderer/shaders/`) are compiled to SPIR-V via `glslangValidator --target-env vulkan1.1` (subgroup ops) as part of the CMake build. Resources are copied to the build directory post-build. `-DDYNAMICAL_BUILD_TOOLS=ON` builds `tools/raycast` (grass rays: bake ahead, OBJ of the grass model, comparison with ray tracing).

On NixOS, a `flake.nix` development shell is provided.

## Architecture

### Game Loop (`src/logic/game.h`)
Entry point is `src/main.cpp` → `Game::start()`. The game loop runs: `renderer->prepare()` → `systems->pre_tick(dt)` → `systems->post_tick(dt)` → `renderer->render()`.

### ECS Pattern
- **Registry**: Centralized `entt::registry` (EnTT 3.16) in `Game`. Context variables: `reg.ctx().emplace<T>()` / `reg.ctx().get<T>()` / `reg.ctx().find<T>()`
- **Components** (`src/logic/components/`): Plain data — `Transform`, `ModelRenderable`, `Camera` (desktop), `VRRig` / `VRInput`, `RenderSettings`, `Input`
- **Systems** (`src/logic/systems/`): Inherit from `System` with virtual `tick()`, managed by `SystemSet` (pre/post tick). Simple ones are declared with `DEFINE_SYSTEM` in `system_list.h`

### Rendering (`src/renderer/`)
- **Two render targets, recorded in parallel every frame**: `ClassicRender` (desktop swapchain) and `VRRender` (OpenXR, one swapchain per eye). Each has its own render pass, framebuffers, command buffers and view UBOs
- **Scene passes** (`scene_pass.h`, `passes/`): `TerrainPass`, `GrassPass`, `ModelPass`, `UIPass` (desktop only). Each builds one pipeline per target; `update()` runs on the main thread, `record()` runs concurrently for both targets and must only read shared state (use `const entt::registry&`)
- **Frame sync** (`renderer.cpp`): per-frame fence; transfer → compute → graphics chained with semaphores; one graphics submit holds both targets' command buffers
- **Context** (`context/`): VRContext (OpenXR instance first, it picks the Vulkan extensions and GPU) → Windu (SDL2) → Instance → Device (+ VMA) → Transfer → Compute → Swapchain
- **Conventions**: world is Y up, -Z forward; projections are Vulkan clip space (Y flipped, depth 0..1); `ViewUBO` is set 0 of every scene pipeline

### Terrain (`src/logic/terrain/`, `src/renderer/terrain/`)
- **Chunks**: 64³ marching cubes of 4 units, density sampled on a 67³ grid (1 sample border) in `[x][z][y]` layout, > 0 is solid
- `ChunkManagerSys` streams chunk entities around the desktop camera and VR rig; `ChunkGeneratorSys` fills `ChunkVolume` with `TerrainGenerator` (FastNoise2) on the taskflow executor
- `TerrainSys` (post tick) records the marching cubes compute shader into the compute command buffer; meshes stay on the GPU (vertex buffer + indirect draw per chunk slot, slots recycled after `NUM_FRAMES + 1` frames)

### Grass (`src/logic/grass/`, `passes/grass_pass.cpp`, docs/grass.md)
- No blade geometry: `GrassPass` draws the chunks again as a shell at the top of the grass; `grass.frag.glsl` looks each view ray up in rays baked against a generated tile of blades repeated to infinity (`GrassTracer`)
- Baked (`bakeGrass`, in `dynamical_sim`): a 3D texture of rays at 45° (x, z, azimuth) and a 2D texture of rays from the top; RGBA8 = hit height, octahedral normal, blade id. A 45° ray is stored where it crosses the **middle** of the grass; the view ray is looked up where it crosses the middle too
- Tile space is sheared around the ground by the lean of the blades (wind, terrain normal): roots do not move
- Several layers (tall ones and an undergrowth) of the same tile at different sizes, heights and noise warps, closest hit wins; patches, blade colours, curved blades, blade shadows: one doc per feature in docs/grass/ (plan.md first)
- `TerrainPass` darkens the terrain and pushes its depth back under the grass (`RenderSettings::grass_pass`): the grass hits may be a little under the real ground
- `dynamical` bakes `resources/grass_raycast.bin` in the run directory when it is missing or its key is stale (never committed). `lookupGrass` mirrors the shader on the CPU (tests `GRASS.*` in `tests/unit/grass.cpp`, `raycast --preview`)

### Physics (`src/physics/`)
- Jolt 5.5, fixed 60 Hz step (`PhysicsSys`, pre tick after gameplay systems), `Transform`s of dynamic bodies interpolated between the last two steps
- Entities get a body by adding `RigidBody` (box or sphere, dynamic / kinematic / static) next to a `Transform`; destroying the entity or removing the component removes the body. Kinematic bodies follow their `Transform` (VR hands)
- **Terrain colliders** (`terrain_colliders.cpp`): the world is cut in cells of 16³ marching cubes; cells overlapping a body's swept bounds this frame are meshed on the CPU (`logic/terrain/marching_cubes.cpp`, shared vertices so Jolt can detect internal edges) and added as static `MeshShape` bodies before the step, in sorted order; cells on the predicted path (1 s) are prefetched on taskflow; idle cells are evicted after 5 s of simulation. Densities come from the loaded chunk volume or straight from `TerrainGenerator::sample`, so collision works without rendering or streaming
- Dynamic bodies use `LinearCast` motion quality and enhanced internal edge removal

### Kinematic walk (`src/logic/agents/`, docs/physical-agents/ik-gait.md)
- Scenes `ik_walk_slope` (tilted plane) and `ik_walk_terrain`; headless `ik_check`, `ik_check_terrain`. Tests `IK.*` and `GAIT.*` in `tests/unit/ik_gait.cpp`
- `RigPose` (joint angles) is written by `GaitBase` (procedural gait), corrected by `Fabrik`, applied by `RigPoseApply` (`MoveKinematic`)
- **IK is component driven**: any system posts `IKTarget` entities for an `IKChain`; `Fabrik` solves them all in one batch (FABRIK, pole, joint limits). `GaitFootIK` owns the leg targets, `GaitBalance` places the pelvis through `solveChain()`
- `GroundField` (plane or marching cubes surface from the density samples) is what feet are planted on, no collider
- A planted foot comes before a joint limit (`GaitParams::yield_limits`): the walk covers any slope, realistic up to about 20°. The body files' ankle swing limits are wider than Unity's export (`unity_y_deg`, `unity_z_deg` keep the originals)

### Models (`src/renderer/model/`)
- glTF 2.0 through tinygltf (`gltf_loader.cpp`), flattened into one vertex/index buffer with node transforms baked in. `ModelManager` (context variable) caches files; instances are entities with `Transform` + `ModelRenderable`

### VR
- `vr_mode` setting: 0 desktop only, 1 VR alongside the desktop when an OpenXR headset is found (falls back silently otherwise)
- `VRActions` (renderer side) writes controller and head poses to the `VRInput` context variable; `VRControlSys` does locomotion (left stick), turning (right stick), throws boxes with the right trigger (hand velocity), and drives the kinematic hand bodies

### Configuration
- Settings serialized to `./config.6.json` via Cereal (JSON), overridable with `key=value` arguments

## Key Dependencies

**vcpkg**: cereal, entt, glm, joltphysics, nlohmann-json, openxr-loader[vulkan], sdl2[vulkan], stb, taskflow, tinygltf (pinned to 3.0.0#1 in `overrides`: the 3.0.0#0 port hash no longer matches GitHub's tarball), vulkan

**Git submodules** (`lib/`): FastNoise2, VulkanMemoryAllocator, ImGui, ImPlot

## Code Conventions

- C++20 standard
- Optick profiler integration (`OPTICK_EVENT` macros) — currently disabled in main
- `ThreadSafe<T>` wrapper in `src/util/util.h` for mutex-guarded shared state
- Taskflow executor (context variable) for chunk generation and parallel command recording
