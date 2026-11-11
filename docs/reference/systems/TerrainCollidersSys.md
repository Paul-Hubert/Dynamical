# TerrainCollidersSys

> **Kind:** system · **Registry name:** `TerrainColliders` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** no
> **Source:** [`src/physics/physics_sys.h`](../../../src/physics/physics_sys.h), [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp), [`src/physics/terrain_colliders.h`](../../../src/physics/terrain_colliders.h), [`src/physics/terrain_colliders.cpp`](../../../src/physics/terrain_colliders.cpp)

## Purpose
Gives the host's (base scene) dynamic bodies something to collide with on the marching-cubes terrain. Before `PhysicsSys` steps, it computes each dynamic body's bounds swept over the steps of this frame and has a private `TerrainColliders` build the static `MeshShape` cells they overlap (synchronously, sorted), while cells on a one-second predicted path are prefetched on the taskflow executor. Idle cells are evicted after 5 s of simulated time.

## Lifecycle
- **Construction / params:** `TerrainCollidersSys(entt::registry&)`; no JSON params. `lookahead = 1.f` s.
- **preinit:** `terrain = make_unique<TerrainColliders>(reg, physicsWorld(reg))` (find-or-emplace `PhysicsWorld`; the constructor copies `std::shared_ptr<TerrainGenerator>` from the context if present, else makes a default `TerrainGenerator`); find-or-emplace `PhysicsStats`.
- **init:** none.
- **tick(dt):**
  1. `clock = fixedStepClock(reg)` (find-or-emplace `FixedStepClock`); `steps = clock.stepsFor(dt)`. `steps <= 0`: return (no build, no eviction, stats untouched).
  2. `fixed_clock = RunArgs::deterministic()` (false without `RunArgs`).
  3. For each `RigidBody` with `motion == Dynamic` and `created()`: `BodyLockRead` through the NoLock interface (skipped if the lock fails); world bounds `b` and linear velocity `v`. `required += grow(b, v * steps * fixed_dt, 1)`; unless `fixed_clock`, `prefetch += grow(b, v * lookahead, 4)`. (`grow` = union of the box and the box moved by the motion, expanded by the margin on all sides.)
  4. `terrain->update(required, prefetch, clock.sim_time)` (see below).
  5. `PhysicsStats`: `colliders_ms` (wall clock of steps 3-4, 0 when `fixed_clock`), `terrain_cells = activeCells()`, `terrain_triangles = triangles()`.
- **TerrainColliders::update(required, prefetch, time):**
  1. Cells overlapping each required bounds (`floor(bounds / 64)` inclusive ranges), sorted by (x, y, z), deduplicated.
  2. New cells get a `Job` (`makeJob`): if a `ChunkMap` context exists and maps the cell's chunk to a valid entity: `ChunkEmpty` -> job empty; `ChunkVolume` -> 17^3 samples copied from `values`. All needed cells get `last_used = time`.
  3. New jobs run: inline when there is no `tf::Executor`, else as a `for_each` taskflow waited on. A job without samples calls `TerrainGenerator::sample(cell * 16, 17^3)`; `MarchingCubes::mesh` with cube size 4; empty mesh or `MeshShapeSettings` error (warning) -> empty; else `MeshShape` with one triangle per 3 indices (material 0).
  4. Every needed cell is activated in sorted order: waits (yield loop) for a prefetched job still running; empty -> `Empty`; else static body at `cell * 64`, friction 0.8, static layer, `DontActivate` -> `Active`, triangles counted.
  5. Prefetch (only with an executor): cells overlapping prefetch bounds that do not exist get a job via `silent_async`; `last_used = max(last_used, time)`.
  6. `Building` cells whose job is done become `Built`.
  7. Eviction: cells with `time - last_used > 5` and not `Building` are erased; `Active` ones have their bodies batch-removed and destroyed.
- **finish:** `terrain->clear()`: waits for all pending jobs, removes and destroys every active cell body, clears cells. The destructor resets `terrain` (whose destructor calls `clear()` again).

## Expected behavior
- Every cell a dynamic body can touch during this frame's steps (with 1 unit margin) has its collider before `PhysicsSys` steps; bodies are added in the same (x, y, z) order whatever the thread timing.
- In deterministic runs (`fixed_frame_dt > 0`) nothing is prefetched and `colliders_ms` is 0.
- Static and kinematic bodies do not request terrain.
- Without `ChunkMap` / loaded chunks (headless), densities come from `TerrainGenerator::sample`; collision does not depend on chunk streaming.
- A chunk tagged `ChunkEmpty` yields no collider for its cells, even if the chunk is fully solid.
- Frames that take 0 steps do nothing (cells are not refreshed or evicted that frame).
- Eviction uses `FixedStepClock::sim_time` (simulated, not wall) seconds.
- A cell still `Building` is never evicted; a `Built` (prefetched, never required) cell is evicted without a body.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | host | readwrite +emplace_ctx | preinit | find-or-emplace; referenced by the TerrainColliders |
| [PhysicsWorld](../components/PhysicsWorld.md) | host | readwrite | tick, finish | body bounds/velocity reads; cell bodies added/removed |
| [PhysicsStats](../components/PhysicsStats.md) | host | write +emplace_ctx | preinit | find-or-emplace |
| [PhysicsStats](../components/PhysicsStats.md) | host | write | tick | `colliders_ms`, `terrain_cells`, `terrain_triangles` |
| [std::shared_ptr&lt;TerrainGenerator&gt;](../components/shared_ptr-TerrainGenerator.md) | host | read | preinit | copied when present |
| [FixedStepClock](../components/FixedStepClock.md) | host | read +emplace_ctx | tick | `stepsFor(dt)`, `sim_time` |
| [RunArgs](../components/RunArgs.md) | host | read | tick | `deterministic()` |
| [RigidBody](../components/RigidBody.md) | host | read | tick | `motion`, `body` |
| [tf::Executor](../components/tf-Executor.md) | host | read | tick | `for_each` (waited), `silent_async` prefetch |
| [ChunkMap](../components/ChunkMap.md) | host | read | tick | chunk entity of a new cell |
| [ChunkEmpty](../components/ChunkEmpty.md) | host | read | tick | `all_of` on the chunk entity |
| [ChunkVolume](../components/ChunkVolume.md) | host | read | tick | `values` copied into the job |

## Side effects
- Creates and destroys static Jolt `MeshShape` bodies (batch `RemoveBodies` / `DestroyBodies` on eviction).
- Runs marching cubes and density sampling on the CPU: blocking taskflow `for_each` for required cells, background `silent_async` tasks for prefetch; busy-wait (yield) when a prefetched cell is needed before its job finished, and in `clear()`.
- Wall-clock timing (`std::chrono::steady_clock`) for `colliders_ms`.
- Warning log when a `MeshShape` cannot be created.

## Ordering and dependencies
- After `RigidBodySys` (bodies must exist to be seen) and before `PhysicsSys` in the same Pre lane: it relies on `stepsFor(dt)` returning the same count `PhysicsSys` will take, which holds only while `PhysicsSys` has not yet added this frame's `dt` to the accumulator. Shipped order: `RigidBody`, `TerrainColliders`, `Physics`.
- After `ChunkGeneratorSys::preinit` if the shared `TerrainGenerator` is to be reused (otherwise a private default generator is used, which matches only if the seeds match).
- Main thread; prefetch tasks only touch their own `Job` and the shared `MarchingCubes` / `TerrainGenerator` (const). `ChunkVolume` data is copied on the main thread.

## Related
[PhysicsSys](PhysicsSys.md), [RigidBodySys](RigidBodySys.md), [WorldTerrainSys](WorldTerrainSys.md), [FixedStepClock](../components/FixedStepClock.md), [`src/physics/`](../folders/src-physics.md), [physics.md](../../physical-agents/physics.md)
