# TerrainColliders

`TerrainCollidersSys` in [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp) (the colliders: `TerrainColliders` in [`src/physics/terrain_colliders.cpp`](../../../src/physics/terrain_colliders.cpp)) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, between `RigidBody` and `Physics` · Parameters: no

Static Jolt mesh colliders of the marching cubes terrain, built on the CPU only where the host's moving bodies are or
are heading. The world is cut in cells of 16³ marching cubes (64 m); cells the dynamic bodies reach over this
frame's physics steps are built synchronously before the step, cells on their path one second ahead are prefetched
on the taskflow executor, idle cells are evicted after 5 s of simulation. Collision therefore works without the
renderer or chunk streaming.

## Behaviour

- **preinit**: makes a `TerrainColliders` on the registry's `PhysicsWorld` (`physicsWorld(reg)`, created when
  missing). Its constructor loads the CPU marching cubes table (`./resources/mclookup.bin`, throws when missing)
  and takes the `std::shared_ptr<TerrainGenerator>` context variable when there is one (else a default generator,
  seed 1337). Makes sure `PhysicsStats` exists. The object is owned by the system, not put in the context.
- **tick**:
  1. Takes and clears the boxes of `TerrainNeeds` (when it exists: the ground under a player's legs).
  2. `steps = FixedStepClock::stepsFor(dt)`: how many fixed steps [Physics](Physics.md) will take this frame (it
     reads the clock before Physics advances it). No step: returns (the needs taken this frame are dropped).
  3. For every created dynamic `RigidBody`: its Jolt world bounds, grown by `velocity × steps/60 s` and a 1 m
     margin, are **required**; grown by `velocity × lookahead` (1 s) and 4 m they are **prefetched**, except in
     deterministic runs (`RunArgs::deterministic()`), which never prefetch. The needs boxes are added to the required.
  4. `TerrainColliders::update(required, prefetch, FixedStepClock::sim_time)`:
     - required cells are sorted (x, y, z) and deduplicated; new ones get a job. `makeJob` copies the 17³ samples
       from the loaded chunk when `ChunkMap` has the chunk (an empty one when it is `ChunkEmpty`, the values of
       `ChunkVolume` otherwise); else the job samples `TerrainGenerator::sample` itself;
     - new required jobs run in parallel on the `tf::Executor` (`for_each`, waited for), or inline without one; then
       every required cell is activated in sorted order: a static `MeshShape` body (friction 0.8,
       `PhysicsLayers::static_layer`) at the cell's corner, waiting for a prefetch still running;
     - prefetched cells not known yet get a job on `silent_async` (only with an executor); bodies are only added
       once a cell is required;
     - cells unused for more than `eviction_delay` (5 s of sim time) and not building are erased, their bodies
       removed and destroyed in one batch.
  5. Writes `PhysicsStats::colliders_ms` (0 in deterministic runs), `terrain_cells` (active cells),
     `terrain_triangles`.
- **finish**: `clear()`: waits for running jobs, removes every collider body, forgets the cells. The destructor
  releases the object.

## Parameters

None. Constants: `TerrainColliders::cell_cubes` 16, `cell_size` 64 m, `eviction_delay` 5 s,
`TerrainCollidersSys::lookahead` 1 s; required margin 1 m, prefetch margin 4 m.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit (`physicsWorld`), tick (`update`), finish (`clear`) | body bounds and velocities read; collider bodies created, added, removed |
| [PhysicsStats](../components/PhysicsStats.md) | context | readwrite+create | preinit, tick | emplaced when missing; `colliders_ms`, `terrain_cells`, `terrain_triangles` written |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | preinit (`TerrainColliders` constructor) | the shared density function, for cells without a loaded chunk |
| [TerrainNeeds](../components/TerrainNeeds.md) | context | readwrite | tick | boxes taken into the required set, then cleared |
| [FixedStepClock](../components/FixedStepClock.md) | context | readwrite+create | tick | `stepsFor(dt)` and `sim_time`; emplaced when missing (by `fixedStepClock`) |
| [RunArgs](../components/RunArgs.md) | context | read | tick (`deterministic`) | no prefetch and no wall-clock timing in fixed-clock runs |
| [RigidBody](../components/RigidBody.md) | component | read | tick | dynamic, created bodies whose bounds need colliders |
| [ChunkMap](../components/ChunkMap.md) | context | read | tick (`makeJob`) | the chunk entity of a cell, when streamed |
| [ChunkEmpty](../components/ChunkEmpty.md) | component | read | tick (`makeJob`) | a cell of an empty chunk needs no mesh |
| [ChunkVolume](../components/ChunkVolume.md) | component | read | tick (`makeJob`) | densities copied from the loaded chunk |
| [Executor](../components/Executor.md) | context | read | tick (`update`) | `tf::Executor`: required cells in parallel, prefetch tasks |

## Side effects

- Creates and destroys static Jolt `MeshShape` bodies (one per non-empty active cell) in the host `PhysicsWorld`,
  always in sorted cell order (deterministic simulation).
- Taskflow work: a waited `for_each` over new required cells, and `silent_async` prefetch jobs (CPU marching cubes
  of 17³ samples, FastNoise2 sampling when no chunk is loaded). Profiler zone `Build collider cell`.
- Reads `./resources/mclookup.bin` in preinit.
- Log warning `Terrain collider : ...` when Jolt refuses a mesh.

## Order and dependencies

- After [RigidBody](RigidBody.md) (new bodies are created and have bounds) and after the systems that fill
  `TerrainNeeds` (`PlayerLegs`); before [Physics](Physics.md), whose steps it predicts with the same
  `FixedStepClock` before Physics advances it.
- Preinit after [ChunkGenerator](ChunkGenerator.md) so that it shares its generator.
- Not for worlds (host scope): [WorldTerrain](WorldTerrain.md) is the world version.

## Used in scenes

<!-- generated -->
- [`base.scene.json`](../../../resources/scenes/base.scene.json): `host.pre`
- [`forest.scene.json`](../../../resources/scenes/forest.scene.json): `host.pre`
- [`forest_herds.scene.json`](../../../resources/scenes/forest_herds.scene.json): `host.pre`
- [`player_fps.scene.json`](../../../resources/scenes/player_fps.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`
- [`player_vr.scene.json`](../../../resources/scenes/player_vr.scene.json): `host.pre`
- [`vegetation_test.scene.json`](../../../resources/scenes/vegetation_test.scene.json): `host.pre`
- [`vr_roller.scene.json`](../../../resources/scenes/vr_roller.scene.json): `host.pre`

## Tests

- `T2.4 base system list after the PhysicsSys split` ([`tests/unit/base_scene.cpp`](../../../tests/unit/base_scene.cpp)).
- Base probe `S0.1 base probe is deterministic`, `S1.1 base scene probe matches the P0 probe` ([`CMakeLists.txt`](../../../CMakeLists.txt), [`tests/golden/base_probe.csv`](../../../tests/golden/base_probe.csv)): thrown boxes landing on the terrain.
- `P1 check: scripted input flies the camera, throws a box and toggles the grass` ([`CMakeLists.txt`](../../../CMakeLists.txt)).
