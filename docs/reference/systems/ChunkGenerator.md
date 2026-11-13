# ChunkGenerator

`ChunkGeneratorSys` in [`src/logic/systems/chunk_systems.cpp`](../../../src/logic/systems/chunk_systems.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre` · Parameters: no

Fills the density volume of the chunk entities [ChunkManager](ChunkManager.md) created, with the world's
`TerrainGenerator` (FastNoise2 heightfield + 3D overhang noise), on the taskflow executor, nearest chunks to the
desktop camera first. A chunk that crosses the surface gets a `ChunkVolume` and `ChunkMeshPending` (the marching
cubes of `TerrainSys` meshes it); a chunk entirely on one side gets `ChunkEmpty` with the mean density.

## Behaviour

- **preinit**: makes a `TerrainGenerator` (seed 1337, `base_level` 60, `height_amplitude` 90, `height_frequency`
  1/700, `overhang_amplitude` 14, `overhang_frequency` 1/90) and emplaces it as the context variable
  `std::shared_ptr<TerrainGenerator>`. Every other user of the terrain density (forests, vegetation, terrain
  colliders, herds, the camera walk) takes this one.
- **tick**:
  1. `collect()`: for every finished job (atomic `done`), if the entity is still valid: removes `ChunkGenerating`,
     adds `ChunkGenerated`, and then either `ChunkVolume` (values moved in, 67³ floats in `[x][z][y]` layout) and
     `ChunkMeshPending` when the chunk crosses the surface, or `ChunkEmpty { mean }` when every sample has the same
     sign. Jobs of destroyed entities are dropped.
  2. With 16 jobs or more in flight, returns.
  3. Candidates: entities with `ChunkC` and none of `ChunkGenerated`, `ChunkGenerating`, `ChunkDestroying`, sorted
     by squared distance from the `Camera` position (origin without a camera; the VR rig is not considered).
  4. Starts `16 - jobs in flight` of the nearest (all of them in a deterministic run): each gets `ChunkGenerating`
     and a `silent_async` task on the `tf::Executor` that calls `TerrainGenerator::generate(pos, values, mean)`
     (profiler zone `Generate chunk`).
  5. In a deterministic run (`RunArgs::deterministic()`, i.e. `fixed_frame_dt > 0`: captures and probes) it then
     waits for every job (zone `Wait for the chunks`) and collects again, so what is loaded at frame N never depends
     on worker timing.
- **finish**: waits for every job still running (they own their data, but must not outlive the registry) and
  clears the list.

Lifecycle of a chunk entity through it: `ChunkC` → `ChunkGenerating` → `ChunkGenerated` + (`ChunkVolume` +
`ChunkMeshPending`) or `ChunkEmpty`.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [TerrainGenerator](../components/TerrainGenerator.md) | context | write+create | preinit | `std::shared_ptr<TerrainGenerator>` emplaced (seed 1337); the jobs hold a copy of the pointer |
| [RunArgs](../components/RunArgs.md) | context | read | tick | `deterministic()`: generate every pending chunk synchronously |
| [Camera](../components/Camera.md) | context | read | tick | position: nearest chunks first |
| [Executor](../components/Executor.md) | context | read | tick | `tf::Executor` the generation jobs run on (`get`: must exist) |
| [ChunkC](../components/ChunkC.md) | component | read | tick | chunk coordinates and centre of the candidates |
| [ChunkGenerating](../components/ChunkGenerating.md) | tag | readwrite+create+destroy | tick, collect | excluded from candidates; added when a job starts, removed when collected |
| [ChunkGenerated](../components/ChunkGenerated.md) | tag | readwrite+create | collect | excluded from candidates; added when the job is collected |
| [ChunkDestroying](../components/ChunkDestroying.md) | tag | read | tick | chunks being unloaded are not generated |
| [ChunkVolume](../components/ChunkVolume.md) | component | write+create | collect | the densities of a chunk that crosses the surface |
| [ChunkMeshPending](../components/ChunkMeshPending.md) | tag | write+create | collect | asks `TerrainSys` for a marching cubes dispatch |
| [ChunkEmpty](../components/ChunkEmpty.md) | component | write+create | collect | chunk with no surface, with its mean density |

## Side effects

- Up to 16 taskflow `silent_async` tasks at a time (unbounded in deterministic runs), each generating 67³ density
  samples with FastNoise2 on a worker thread.
- Blocks the main thread in deterministic runs until every started job is done, and in finish.
- Profiler: zones `Generate chunk` (workers), `Wait for the chunks`; plot `Chunk jobs`.

## Order and dependencies

- After [ChunkManager](ChunkManager.md) (it generates what the manager created this frame).
- Its preinit must come before the preinit of systems that take the shared generator:
  [TerrainColliders](TerrainColliders.md) (`TerrainColliders` constructor, else it makes its own default one, which
  gives the same densities only because the seed is the same), `Forest`, `VegetationScatter`, `VegetationLineup`.
- Needs the `tf::Executor` context variable (emplaced by `Game`): not usable in a world.
- `TerrainSys` (post) meshes the `ChunkMeshPending` chunks; `ForestSys`, `VegetationScatter`, the colliders and the
  ray tracing read `ChunkVolume`.

## Used in scenes
<!-- generated -->

## Tests

- `T2.4 base system list after the PhysicsSys split` ([`tests/unit/base_scene.cpp`](../../../tests/unit/base_scene.cpp)).
- Base captures and probe (`V0.1`, `V0.2`, `S0.1`, `S1.1`, `V2.2` in [`CMakeLists.txt`](../../../CMakeLists.txt)): deterministic generation in fixed-clock runs.
- The generator itself is used directly by `FOREST.*`, `VEG.*`, `TW.*`, `IK.*` / `GAIT.*` ([`tests/unit/forest.cpp`](../../../tests/unit/forest.cpp), [`tests/unit/vegetation.cpp`](../../../tests/unit/vegetation.cpp), [`tests/unit/terrain_walker.cpp`](../../../tests/unit/terrain_walker.cpp), [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp)); no test runs the system alone.
