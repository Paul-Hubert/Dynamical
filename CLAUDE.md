# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Dynamical is a C++20 Vulkan-based simulation engine featuring procedural terrain, water flow, AI entities, and particle systems. It uses an ECS architecture powered by EnTT.

## Build System

Uses CMake with vcpkg for dependency management. Generator is Visual Studio 17 2022.

```bash
# Configure (vcpkg toolchain is set via CMakePresets.json)
cmake --preset default

# Build
cmake --build build

# The executable is output to the project root
```

Shaders (`.glsl` in `src/renderer/shaders/`) are compiled to SPIR-V via `glslangValidator` as part of the CMake build. Resources are copied to the build directory post-build.

On NixOS, a `flake.nix` development shell is provided.

## Architecture

### Game Loop (`src/logic/game.h`)
Entry point is `src/main.cpp` → `Game::start()`. The game loop runs: `renderer->prepare()` → `systems->pre_tick(dt)` → `systems->post_tick(dt)` → `renderer->render()`.

### ECS Pattern
- **Registry**: Centralized `entt::registry` in `Game`
- **Components** (`src/logic/components/`): Plain data structs — Position, Object, BasicNeeds, Renderable, WaterFlow, etc.
- **Systems** (`src/logic/systems/`): Inherit from `System` base class with virtual `tick()`. Managed by `SystemSet` which handles pre/post tick ordering.
- **System registration**: Declared via macros in `system_list.h`

### Rendering (`src/renderer/`)
- **Triple buffering**: `NUM_FRAMES = 3` (in `context/num_frames.h`)
- **Semaphore chain**: Transfer → Compute → Graphics queues with per-frame synchronization
- **Context hierarchy**: Windu (SDL2 window) → Instance → Device (+ VMA allocator) → Swapchain → ClassicRender
- **Terrain shader**: Uses a GPU-side hash table mapping chunk coordinates to tile buffer indices (`maprender.vert.glsl`)

### Map/Terrain (`src/logic/map/`)
- **Chunks**: 32×32 tile grids, lazy-generated via `MapManager`
- **Tiles**: Type enum (dirt, grass, stone, sand, water, river), height, optional entity reference
- **Position component**: Managed exclusively by `MapManager` — do not modify directly
- **Procedural generation**: Uses FastNoise2 submodule

### AI (`src/ai/`)
- **Behaviors** (`behaviors/`): Score-based selection (e.g., WanderBehavior, EatBehavior)
- **Actions** (`actions/`): Linked-list chain pattern — each action can chain to next or be interrupted
- **AIC component** (`aic.h`): Holds current action + behavior score per entity

### Configuration
- Settings serialized to `./config.5.json` via Cereal (JSON)
- Map configuration stored per-map in settings

## Key Dependencies

**vcpkg**: cereal, glm, sdl2[vulkan], taskflow, stb, vulkan
**Git submodules** (`lib/`): FastNoise2, VulkanMemoryAllocator, ImGui, ImPlot, EnTT

## Code Conventions

- C++20 standard
- Optick profiler integration (`OPTICK_EVENT` macros) — currently disabled in main
- `ThreadSafe<T>` wrapper in `src/util/util.h` for mutex-guarded shared state
- Taskflow executor used for parallel system execution
