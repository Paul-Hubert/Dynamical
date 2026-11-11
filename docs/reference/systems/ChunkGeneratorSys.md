# ChunkGeneratorSys

> **Kind:** system · **Registry name:** `ChunkGenerator` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** no
> **Source:** [`src/logic/systems/chunk_systems.h`](../../../src/logic/systems/chunk_systems.h), [`src/logic/systems/chunk_systems.cpp`](../../../src/logic/systems/chunk_systems.cpp)

## Purpose
Fills the densities of the chunks created by [ChunkManagerSys](ChunkManagerSys.md): it owns the host `TerrainGenerator` (published as the `std::shared_ptr<TerrainGenerator>` context variable), starts at most 16 concurrent `TerrainGenerator::generate` jobs on the taskflow executor, nearest chunks to the desktop camera first, and on completion tags each chunk `ChunkGenerated` plus either `ChunkVolume` + `ChunkMeshPending` (the chunk crosses the surface) or `ChunkEmpty`. In deterministic (fixed frame clock) runs it generates every pending chunk synchronously within the frame.

## Lifecycle
- **Construction / params:** `ChunkGeneratorSys(reg)`, no params.
- **preinit:** `generator = make_shared<TerrainGenerator>()` (seed 1337, default parameters); `reg.ctx().emplace<std::shared_ptr<TerrainGenerator>>(generator)`. EnTT's `ctx().emplace` keeps an existing value, so if another host system emplaced one earlier, the context holds that one while the jobs use this system's own `generator`.
- **init:** None.
- **tick(dt):** `dt` unused.
  1. `sync = RunArgs present && RunArgs::deterministic()` (`fixed_frame_dt > 0`).
  2. `collect()`: for every finished job (`done` atomic), if the entity is still valid: `remove<ChunkGenerating>`, `emplace<ChunkGenerated>`, then if `surface` `emplace<ChunkVolume>` (values moved in) and `emplace<ChunkMeshPending>`, else `emplace<ChunkEmpty>(mean)`. The job is dropped either way.
  3. If `jobs.size() >= 16`, return.
  4. Priority centre: `Camera::position`, or the origin without a `Camera` (the VR rig is not used).
  5. Candidates: `view<ChunkC>(exclude<ChunkGenerated, ChunkGenerating, ChunkDestroying>)` with squared distance from `center()`. `count = sync ? all : min(candidates, 16 - jobs)`; `partial_sort` the first `count`.
  6. For each of them: `emplace<ChunkGenerating>`, create a `Job { entity, pos }`, and `executor.silent_async` a task running `generator->generate(pos, values, mean)` then `done = true`.
  7. If `sync`: busy-wait (`std::this_thread::yield`) for every job, then `collect()` again.
- **finish:** busy-waits for every running job and clears the list (the jobs own their data; this only ensures no task outlives the registry).

## Expected behavior
- A chunk is in exactly one state: pending (`ChunkC` only), generating (`ChunkGenerating`), or generated (`ChunkGenerated` + exactly one of `ChunkVolume` or `ChunkEmpty`); `ChunkMeshPending` accompanies `ChunkVolume` until TerrainSys meshes it.
- `ChunkVolume::values` has `chunk::num_values` (67^3) samples in `[x][z][y]` layout; it is emplaced only when there is at least one sample > 0 and one <= 0.
- Never more than 16 jobs in flight in non-deterministic runs; in deterministic runs, after the tick no chunk created before it is left pending or generating, so what is loaded at frame N does not depend on worker timing.
- A chunk tagged `ChunkDestroying` while generating still gets its results (TerrainSys waits for `ChunkGenerating` to go before destroying it); a pending chunk tagged `ChunkDestroying` is never generated.
- If the entity was destroyed meanwhile (`!reg.valid`), the result is discarded.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [std::shared_ptr&lt;TerrainGenerator&gt;](../components/shared_ptr-TerrainGenerator.md) | host | write (emplace_ctx) | preinit | new generator, seed 1337 |
| [RunArgs](../components/RunArgs.md) | host | read | tick | `find`; `deterministic()` |
| [Camera](../components/Camera.md) | host | read | tick | `find`; `position` for priority |
| [ChunkC](../components/ChunkC.md) | host | read | tick | `center()`, `pos` |
| [ChunkGenerating](../components/ChunkGenerating.md) | host | readwrite +create +remove | tick | exclusion filter; emplaced at job start, removed in collect |
| [ChunkGenerated](../components/ChunkGenerated.md) | host | readwrite +create | tick | exclusion filter; emplaced in collect |
| [ChunkDestroying](../components/ChunkDestroying.md) | host | read | tick | exclusion filter |
| [ChunkVolume](../components/ChunkVolume.md) | host | write +create | tick | collect, surface chunks |
| [ChunkMeshPending](../components/ChunkMeshPending.md) | host | write +create | tick | collect, surface chunks |
| [ChunkEmpty](../components/ChunkEmpty.md) | host | write +create | tick | collect, chunks without surface |
| [tf::Executor](../components/tf-Executor.md) | host | readwrite | tick | `get`; `silent_async` per job |

The component-doc file names for `std::shared_ptr<TerrainGenerator>` and `tf::Executor` above are guesses at the merge script's naming.

## Side effects
- Spawns up to 16 (or, when deterministic, all pending) `TerrainGenerator::generate` tasks on the shared taskflow executor; each task writes only its own `Job` (shared_ptr owned by both the task and the system).
- Busy-waits with `std::this_thread::yield` in deterministic ticks and in `finish`.
- `OPTICK_EVENT` profiling marker.

## Ordering and dependencies
- After [ChunkManagerSys](ChunkManagerSys.md) (new chunks of this frame become candidates in the same tick) and before `Terrain` (post lane), which meshes `ChunkMeshPending` chunks and destroys `ChunkDestroying` ones.
- Needs `tf::Executor` in the host context (emplaced by `Game`); `get` throws/asserts without it.
- `TerrainCollidersSys` / `PlayerControl` / `VRControl` find the generator in the context; with the default orders the host `std::shared_ptr<TerrainGenerator>` is the one emplaced here.
- Registry access happens on the main thread only; workers touch only `Job` data and the thread-safe `TerrainGenerator`.

## Related
- [ChunkManagerSys](ChunkManagerSys.md), [TerrainSys](TerrainSys.md), [TerrainCollidersSys](TerrainCollidersSys.md), [WorldTerrainSys](WorldTerrainSys.md)
- [ChunkVolume](../components/ChunkVolume.md), [ChunkEmpty](../components/ChunkEmpty.md), [ChunkGenerating](../components/ChunkGenerating.md), [ChunkGenerated](../components/ChunkGenerated.md), [ChunkMeshPending](../components/ChunkMeshPending.md)
