# WorldTerrain

`WorldTerrainSys` in [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp) (the colliders: [`src/physics/terrain_colliders.cpp`](../../../src/physics/terrain_colliders.cpp)) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after `RigidBody`, before `PhysicsStep` · Parameters: yes

The marching cubes ground of a world (sphere rolling, terrain walker, animals on terrain): the same
`TerrainColliders` as the host's [TerrainColliders](TerrainColliders.md), on the world's own `PhysicsWorld`, built
inline before each step around every dynamic body and around the ragdoll's segments, never prefetched. It also gives
the world its `TerrainGenerator` and shares the colliders as a context variable, which agents use to spawn on the
ground (`TerrainColliders::require`, `surfaceHeight`).

## Behaviour

- **constructor**: reads `seed`, `margin`, `below` from the params.
- **preinit**: emplaces `std::shared_ptr<TerrainGenerator>` (a generator with `seed`; `emplace` keeps an existing
  one), makes the `TerrainColliders` on the world's `PhysicsWorld` (loads `./resources/mclookup.bin`, takes that
  generator) and emplaces it as `std::shared_ptr<TerrainColliders>`.
- **tick(h)**:
  1. For every created dynamic `RigidBody`: its world bounds grown by `velocity × h` and `margin`, then lowered by
     `below` (the ground under a falling body is there).
  2. When `TerrainUsers` exists and has bodies (the segments of a ragdoll that are no `RigidBody`): one box around
     all of them, each grown the same way, lowered by `below`.
  3. `TerrainColliders::update(required, {}, WorldClock::tick × WorldClock::dt)`: required cells built inline in
     sorted order (a world has no `tf::Executor`), no prefetch, idle cells evicted after 5 s of world time. With no
     `ChunkMap` in a world, every cell samples the generator.
- **finish**: `clear()`: every collider body removed.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `seed` | int | 1337 | seed of the world's `TerrainGenerator` (the host's terrain is 1337) |
| `margin` | float | 2.0 | metres added around each body's swept bounds |
| `below` | float | 6.0 | metres the bounds are extended downward |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [TerrainGenerator](../components/TerrainGenerator.md) | context | write+create | preinit | `std::shared_ptr<TerrainGenerator>` with `seed`, read by the colliders and by the world's agents |
| [TerrainColliders](../components/TerrainColliders.md) | context | write+create | preinit | `std::shared_ptr<TerrainColliders>`, shared with the world's systems |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit (`physicsWorld`), tick, finish | body bounds read; collider bodies added and removed |
| [WorldClock](../components/WorldClock.md) | context | read | tick | `tick × dt`: the time of the cells' last use |
| [RigidBody](../components/RigidBody.md) | component | read | tick | dynamic, created bodies needing ground |
| [TerrainUsers](../components/TerrainUsers.md) | context | read | tick | ragdoll segments needing ground |
| [ChunkMap](../components/ChunkMap.md) | context | read | tick (`makeJob`) | looked up, absent in worlds: cells always sample the generator |
| [Executor](../components/Executor.md) | context | read | tick (`update`) | looked up, absent in worlds: cells built inline |

## Side effects

- Creates and destroys static Jolt `MeshShape` bodies in the world's `PhysicsWorld`, in sorted cell order.
- CPU marching cubes and FastNoise2 sampling on the calling thread (the world's task in `Worlds::step`).
- Reads `./resources/mclookup.bin` in preinit; log warning `Terrain collider : ...` on a refused mesh.

## Order and dependencies

- After [RigidBody](RigidBody.md) (bodies created) and after the systems that set `TerrainUsers` (agent factories
  set it when the context has `std::shared_ptr<TerrainColliders>`), before [PhysicsStep](PhysicsStep.md).
- Its preinit before the world systems that read `std::shared_ptr<TerrainColliders>` or `TerrainGenerator` in their
  preinit or init (spawning on the ground, `GroundTerrain`, `WalkRoutes`).

## Used in scenes

<!-- generated -->
- [`common/animal_terrain_world.json`](../../../resources/scenes/common/animal_terrain_world.json): `fixed`
- [`common/humanbot_terrain_world.json`](../../../resources/scenes/common/humanbot_terrain_world.json): `fixed`
- [`common/sphere_world.json`](../../../resources/scenes/common/sphere_world.json): `fixed`
- [`sphere_view.scene.json`](../../../resources/scenes/sphere_view.scene.json): `fixed`
- [`sphere_view_heuristic.scene.json`](../../../resources/scenes/sphere_view_heuristic.scene.json): `fixed`
- [`vr_roller.scene.json`](../../../resources/scenes/vr_roller.scene.json): `fixed`
- [`wheel_view.scene.json`](../../../resources/scenes/wheel_view.scene.json): `fixed`

## Tests

- `SR.11 marching cubes ground: the sphere spawns on it and the proportional controller beats zero torque` ([`tests/unit/sphere_roller.cpp`](../../../tests/unit/sphere_roller.cpp)).
- `TW.6` (a limp ragdoll lands on the colliders through `TerrainUsers`), `TW.8` (terrain worlds are deterministic and independent), `TW.10` ([`tests/unit/terrain_walker.cpp`](../../../tests/unit/terrain_walker.cpp)): `walker_terrain_train` and the terrain demo worlds.
