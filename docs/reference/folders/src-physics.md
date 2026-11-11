# `src/physics/`

> [Source](../../../src/physics/) · Parent: [`src/`](src.md)

## Summary
Jolt Physics integration, part of the `dynamical_sim` library (no renderer dependency). `JoltRuntime` holds Jolt's process-wide state and must outlive every `PhysicsWorld`. Each registry (host and every world) has its own `PhysicsWorld` context variable: the Jolt `PhysicsSystem` with its job system, temp allocator and object layers (static, moving, agent, sensor). Five systems use it: `RigidBodySys` creates and destroys the bodies of `RigidBody`, `Ground` and `HeightField` components (host and worlds); the host steps with `PhysicsSys` (60 Hz `FixedStepClock` accumulator, interpolated Transforms) and gets marching-cubes colliders from `TerrainCollidersSys`; worlds step once per fixed tick with `PhysicsStepSys` and get their terrain from `WorldTerrainSys`. `TerrainColliders` meshes 64-unit terrain cells on the CPU into static `MeshShape` bodies where bodies are or are heading. The component types (`RigidBody`, `Ground`, `HeightField`, `PhysicsStats`) are declared in [`src/logic/components/rigid_body.h`](../../../src/logic/components/rigid_body.h); `HeightField::height` is implemented here.

## Files
| File | Contents |
|---|---|
| [`jolt_runtime.h`](../../../src/physics/jolt_runtime.h) / [`.cpp`](../../../src/physics/jolt_runtime.cpp) | `JoltRuntime`: RAII install of counted allocator hooks, trace/assert hooks, `Factory` and `RegisterTypes`; `active()`, `liveAllocations()` (leak checks). One per main and test main. |
| [`physics_world.h`](../../../src/physics/physics_world.h) / [`.cpp`](../../../src/physics/physics_world.cpp) | `PhysicsLayers` (object/broad-phase layers and collision table), layer filter classes, `Combine`, `PhysicsConfig`, `PhysicsWorld` (Jolt system, `step`, static-only queries `groundBelow` / `castStatic` / `sphereContact`), `physicsWorld(reg)` find-or-emplace, glm/Jolt conversions `toJolt` / `toGlm`. |
| [`physics_sys.h`](../../../src/physics/physics_sys.h) / [`.cpp`](../../../src/physics/physics_sys.cpp) | `PhysicsPose`, `FixedStepClock` (`stepsFor`), systems `RigidBodySys`, `TerrainCollidersSys`, `WorldTerrainSys`, `PhysicsSys`, `PhysicsStepSys`. |
| [`terrain_colliders.h`](../../../src/physics/terrain_colliders.h) / [`.cpp`](../../../src/physics/terrain_colliders.cpp) | `Bounds`; `TerrainColliders`: cell cache (16^3 cubes per cell), `update(required, prefetch, time)`, `require`, `surfaceHeight`, `overhangAmplitude`, `clear`, stats; jobs on taskflow or inline. |
| [`height_field.h`](../../../src/physics/height_field.h) / [`.cpp`](../../../src/physics/height_field.cpp) | `drawWaves(HeightField&, seed)`, `sampleHeights(const HeightField&)`, and `HeightField::height` (sum of plane waves). |

## Systems defined here
- [RigidBodySys](../systems/RigidBodySys.md)
- [TerrainCollidersSys](../systems/TerrainCollidersSys.md)
- [WorldTerrainSys](../systems/WorldTerrainSys.md)
- [PhysicsSys](../systems/PhysicsSys.md)
- [PhysicsStepSys](../systems/PhysicsStepSys.md)

## Components defined here
- [PhysicsPose](../components/PhysicsPose.md) (component)
- [FixedStepClock](../components/FixedStepClock.md) (host context)
- [PhysicsWorld](../components/PhysicsWorld.md) (context, every registry)
- [std::shared_ptr&lt;TerrainColliders&gt;](../components/shared_ptr-TerrainColliders.md) (world context)

## Subfolders
None.
