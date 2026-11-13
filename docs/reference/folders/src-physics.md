# src/physics

Jolt Physics in the engine: the process-wide runtime, a `PhysicsWorld` per registry, the systems that make bodies
and step them on the host (60 Hz accumulator, interpolation) and in worlds (one fixed tick), and the marching cubes
terrain colliders built where bodies go.

| File | What |
|---|---|
| [`jolt_runtime.h`](../../../src/physics/jolt_runtime.h) / [`jolt_runtime.cpp`](../../../src/physics/jolt_runtime.cpp) | `JoltRuntime`: Jolt's allocator (counted: `liveAllocations` for leak checks), trace and assert hooks, factory and type registration; one per main, outlives every world |
| [`physics_world.h`](../../../src/physics/physics_world.h) / [`physics_world.cpp`](../../../src/physics/physics_world.cpp) | `PhysicsLayers` (static, moving, agent, sensor, animal and who collides with whom), broad phase filters, `PhysicsConfig`, `PhysicsWorld` (Jolt system + job system + temp allocator; `step`, `groundBelow`, `castStatic`, `sphereContact`), `physicsWorld(reg)`, `toJolt` / `toGlm` |
| [`physics_sys.h`](../../../src/physics/physics_sys.h) / [`physics_sys.cpp`](../../../src/physics/physics_sys.cpp) | `PhysicsPose`, `FixedStepClock`, `FixedSteps` (+ `fixedSteps(reg)`), `TerrainNeeds`; systems `RigidBodySys`, `TerrainCollidersSys`, `WorldTerrainSys`, `PhysicsSys`, `PhysicsStepSys` |
| [`terrain_colliders.h`](../../../src/physics/terrain_colliders.h) / [`terrain_colliders.cpp`](../../../src/physics/terrain_colliders.cpp) | `Bounds`, `TerrainUsers`, `TerrainColliders`: cells of 16³ cubes meshed on the CPU (shared vertices), static `MeshShape` bodies added in sorted order, prefetch on taskflow, eviction after 5 s; densities from the loaded chunk or the generator |
| [`height_field.h`](../../../src/physics/height_field.h) / [`height_field.cpp`](../../../src/physics/height_field.cpp) | `HeightField::height`, `drawWaves` (log-uniform wavelengths, equal steepest slope), `sampleHeights` (Jolt's row-major layout) |

## Systems registered from here

(Registered in [`src/scene/system_registry.cpp`](../../../src/scene/system_registry.cpp).)

- [RigidBody](../systems/RigidBody.md) (`RigidBodySys`, host and world)
- [TerrainColliders](../systems/TerrainColliders.md) (`TerrainCollidersSys`, host)
- [Physics](../systems/Physics.md) (`PhysicsSys`, host)
- [PhysicsStep](../systems/PhysicsStep.md) (`PhysicsStepSys`, world)
- [WorldTerrain](../systems/WorldTerrain.md) (`WorldTerrainSys`, world)

## Components and context defined here

- [PhysicsPose](../components/PhysicsPose.md) (component of dynamic bodies)
- [PhysicsWorld](../components/PhysicsWorld.md) (context, one per registry)
- [FixedStepClock](../components/FixedStepClock.md), [FixedSteps](../components/FixedSteps.md), [TerrainNeeds](../components/TerrainNeeds.md) (host context)
- [TerrainUsers](../components/TerrainUsers.md), [TerrainColliders](../components/TerrainColliders.md) (world context; `std::shared_ptr<TerrainColliders>`)

## Main functions and classes

- `physicsWorld(reg)`: the registry's `PhysicsWorld`, made with the default config when missing (the host).
- `fixedSteps(reg).before / after`: what host systems run at the physics rate (add in preinit, `remove(this)` in finish).
- `PhysicsWorld::groundBelow`, `castStatic`, `sphereContact`: queries of STATIC geometry, outside the step.
- `TerrainColliders::require(bounds, time)`, `surfaceHeight`: spawn on the terrain.
- `PhysicsLayers::collide`: agents never collide with each other (self-collision is a group filter), animals do.

## Dependencies

Jolt, taskflow, [`src/logic/components`](src-logic-components.md) (`RigidBody`, `Ground`, `HeightField`,
`PhysicsStats`, `Transform`), [`src/logic/terrain`](src-logic-terrain.md) (chunks, `MarchingCubes`,
`TerrainGenerator`), [`src/scene`](src-scene.md) (`RunArgs`, `WorldClock`, `WorldRng`), [`src/util`](src-util.md).

## Design docs

[docs/physical-agents/physics.md](../../physical-agents/physics.md), [docs/physical-agents/jolt-readouts.md](../../physical-agents/jolt-readouts.md),
[docs/physical-agents/simulation.md](../../physical-agents/simulation.md), [docs/players.md](../../players.md) (`FixedSteps`, `TerrainNeeds`),
[docs/port-mc-grass-gltf-vr-jolt.md](../../port-mc-grass-gltf-vr-jolt.md).
