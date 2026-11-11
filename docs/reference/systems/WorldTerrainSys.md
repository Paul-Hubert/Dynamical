# WorldTerrainSys

> **Kind:** system · **Registry name:** `WorldTerrain` · **Scope:** World · **Lane:** Fixed (Phase Every) · **Needs renderer:** no
> **Source:** [`src/physics/physics_sys.h`](../../../src/physics/physics_sys.h), [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp), [`src/physics/terrain_colliders.h`](../../../src/physics/terrain_colliders.h), [`src/physics/terrain_colliders.cpp`](../../../src/physics/terrain_colliders.cpp)

## Purpose
The marching-cubes ground of a world (e.g. `sphere_world`): its own `TerrainGenerator(seed)` and `TerrainColliders` on the world's `PhysicsWorld`, built inline every fixed tick around every dynamic body (bounds swept over one tick, grown by `margin`, extended `below` downward). The colliders are shared as the world context variable `std::shared_ptr<TerrainColliders>` so agents can require cells and query the surface height.

## Lifecycle
- **Construction / params:** `WorldTerrainSys(reg, params)`. JSON params (via `nlohmann::json::value`, no validation; wrong types throw from nlohmann):
  - `seed` (int, default 1337): `TerrainGenerator` seed (base's terrain is 1337).
  - `margin` (float, default 2.0): metres added on every side of the swept bounds.
  - `below` (float, default 6.0): metres added below the swept bounds.
- **preinit:** `reg.ctx().emplace<std::shared_ptr<TerrainGenerator>>(make_shared<TerrainGenerator>(seed))`; `terrain = make_shared<TerrainColliders>(reg, physicsWorld(reg))` (the constructor picks up the generator just emplaced); `reg.ctx().emplace<std::shared_ptr<TerrainColliders>>(terrain)`. (EnTT `ctx().emplace` keeps an existing variable of that type.)
- **init:** none.
- **tick(h):**
  1. `world = physicsWorld(reg)`, `clock = ctx().get<WorldClock>()` (throws/asserts if missing; worlds always have it).
  2. For each `RigidBody` with `motion == Dynamic` and `created()`: `BodyLockRead` (NoLock), skipped if it fails; `bounds = grow(world AABB, linear velocity * h, margin)`, then `bounds.min.y -= below`.
  3. `terrain->update(required, {}, clock.tick * clock.dt)`: no prefetch. World registries have no `tf::Executor`, so new cells are meshed inline in sorted order; no `ChunkMap`, so densities come from `TerrainGenerator::sample`. See [TerrainCollidersSys](TerrainCollidersSys.md) for the full `update` algorithm (cells, activation, eviction after 5 s).
- **finish:** `terrain->clear()` (removes and destroys all active cell bodies). The context variable keeps the (now empty) `TerrainColliders` alive until the registry is destroyed.

## Expected behavior
- Before `PhysicsStep` in the same tick, every 64-unit terrain cell a dynamic body can reach within one tick (plus margins) has a static `MeshShape` collider (friction 0.8, static layer).
- Fully deterministic: no background work, cells added in sorted (x, y, z) order, eviction on world time `tick * dt`.
- Time passed is the clock value before `World::tick` increments `tick`, i.e. the start of the current tick.
- No stats are written (`PhysicsStats` is not touched).
- With several worlds, each has its own generator, cells and bodies (no sharing across worlds).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [std::shared_ptr&lt;TerrainGenerator&gt;](../components/shared_ptr-TerrainGenerator.md) | world | write +emplace_ctx | preinit | `TerrainGenerator(seed)`, read back by TerrainColliders |
| [std::shared_ptr&lt;TerrainColliders&gt;](../components/shared_ptr-TerrainColliders.md) | world | write +emplace_ctx | preinit | the shared collider cache |
| [PhysicsWorld](../components/PhysicsWorld.md) | world | readwrite | preinit, tick, finish | bound to TerrainColliders; body reads; cell bodies added/removed |
| [WorldClock](../components/WorldClock.md) | world | read | tick | `tick`, `dt` |
| [RigidBody](../components/RigidBody.md) | world | read | tick | `motion`, `body` |
| [tf::Executor](../components/tf-Executor.md) | world | read | tick | looked up, absent in worlds |
| [ChunkMap](../components/ChunkMap.md) | world | read | tick | looked up, absent in worlds |

## Side effects
- Creates and destroys static Jolt `MeshShape` bodies in the world's `PhysicsWorld`.
- CPU density sampling and marching cubes inline on the world's thread.
- Warning log when a `MeshShape` cannot be created.

## Ordering and dependencies
- After `RigidBodySys` and before `PhysicsStepSys` in the fixed lane (shipped `sphere_world`: `RigidBody`, `WorldTerrain`, ..., `PhysicsStep`).
- Its `preinit` must precede any system that reads `std::shared_ptr<TerrainColliders>` / `std::shared_ptr<TerrainGenerator>` (e.g. the sphere roller's spawn, which calls `require` and `surfaceHeight`).
- Runs on the world's task; worlds step in parallel, each touching only its own registry.

## Related
[TerrainCollidersSys](TerrainCollidersSys.md), [PhysicsStepSys](PhysicsStepSys.md), [RigidBodySys](RigidBodySys.md), [std::shared_ptr&lt;TerrainColliders&gt;](../components/shared_ptr-TerrainColliders.md), [`src/physics/`](../folders/src-physics.md), [sphere-roller.md](../../physical-agents/sphere-roller.md)
