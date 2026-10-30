# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Dynamical is a C++20 Vulkan engine with a marching cubes voxel world (GPU meshing), raymarched grass, glTF models, and OpenXR VR rendering alongside the desktop window. It uses an ECS architecture powered by EnTT.

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

Shaders (`.glsl` in `src/renderer/shaders/`) are compiled to SPIR-V via `glslangValidator --target-env vulkan1.1` (subgroup ops) as part of the CMake build. Resources are copied to the build directory post-build. `-DDYNAMICAL_BUILD_TOOLS=ON` builds `tools/raycast`, which bakes `resources/raycast_data.bin` for the grass shader.

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

### Models (`src/renderer/model/`)
- glTF 2.0 through tinygltf (`gltf_loader.cpp`), flattened into one vertex/index buffer with node transforms baked in. `ModelManager` (context variable) caches files; instances are entities with `Transform` + `ModelRenderable`

### VR
- `vr_mode` setting: 0 desktop only, 1 VR alongside the desktop when an OpenXR headset is found (falls back silently otherwise)
- `VRActions` (renderer side) writes controller and head poses to the `VRInput` context variable; `VRControlSys` does locomotion (left stick), turning (right stick), and spawns boxes with the right trigger

### Configuration
- Settings serialized to `./config.6.json` via Cereal (JSON), overridable with `key=value` arguments

## Key Dependencies

**vcpkg**: cereal, entt, glm, nlohmann-json, openxr-loader[vulkan], sdl2[vulkan], stb, taskflow, tinygltf (pinned to 3.0.0#1 in `overrides`: the 3.0.0#0 port hash no longer matches GitHub's tarball), vulkan

Physics : Bullet (used on the old `vr` branch) is not ported, Jolt is the planned replacement (see the physical-agents docs on the `docs/physical-agents` branch).
**Git submodules** (`lib/`): FastNoise2, VulkanMemoryAllocator, ImGui, ImPlot

## Code Conventions

- C++20 standard
- Optick profiler integration (`OPTICK_EVENT` macros) — currently disabled in main
- `ThreadSafe<T>` wrapper in `src/util/util.h` for mutex-guarded shared state
- Taskflow executor (context variable) for chunk generation and parallel command recording
