# src/util

Small helpers with no knowledge of the engine: logging, paths, profiling macros, primitive meshes, and a few
leftovers of the 2019 engine. Part of `dynamical_sim`; nothing here touches a registry.

| File | What |
|---|---|
| [`entt_util.h`](../../../src/util/entt_util.h) | `Alias<hash, T>` and `Entity<hash>`: tagged wrappers to keep several values of one type apart in a registry context |
| [`log.h`](../../../src/util/log.h) / [`log.cpp`](../../../src/util/log.cpp) | `dy::log(level)` stream (trace..critical); `setLogFile`: tees stdout and stderr to a file flushed at each line (`dynamical_train -logFile`) |
| [`paths.h`](../../../src/util/paths.h) / [`paths.cpp`](../../../src/util/paths.cpp) | `executableDirectory()`; `libraryResourceFile()`: a file under `resources/` here, as given, next to the executable, else in the source tree (`DY_SOURCE_RESOURCES`: the generated animal and vegetation libraries) |
| [`primitive_mesh.h`](../../../src/util/primitive_mesh.h) / [`primitive_mesh.cpp`](../../../src/util/primitive_mesh.cpp) | `MeshData`, `capsuleMesh(radius, half_height)` (Jolt's capsule along Y) and `sphereMesh(radius)`: the primitives `ModelManager` and `WorldView` draw |
| [`profile.h`](../../../src/util/profile.h) / [`profile.cpp`](../../../src/util/profile.cpp) | Tracy wrapper: `profile::start/stop/active/threadName/message`, macros `DY_ZONE`, `DY_ZONE_N`, `DY_ZONE_DYNAMIC`, `DY_FRAME`, `DY_FRAME_NAMED`, `DY_PLOT` (empty without `TRACY_ENABLE`, one branch until started) |
| [`profile_workers.h`](../../../src/util/profile_workers.h) | `profile::namedWorkers()`: a taskflow worker interface that names executor threads `Worker <n>` |
| [`quad_tree.h`](../../../src/util/quad_tree.h) | `QuadTree<T>` (an octree despite the name) with cereal serialisation, from the 2019 engine; included by nothing |
| [`tiled_noise.h`](../../../src/util/tiled_noise.h) | `TiledNoise`: tileable Perlin noise of the old engine; included by nothing |
| [`util.h`](../../../src/util/util.h) / [`util.cpp`](../../../src/util/util.cpp) | `readFile`, `ThreadSafe<T>` (mutex-guarded value, `*x` gives a locking accessor), `sq`, `c_pow`, `lerp`, small vector helpers |

## Systems registered here

None.

## Components and context defined here

None (`entt_util.h`'s wrappers are types to build context variables with; no variable of them is in use).

## Main functions and classes

- `dy::log(dy::warning) << ...`: the one log of the engine; `setLogFile` for the trainer's health check.
- `DY_ZONE*` / `DY_PLOT` / `DY_FRAME`: see [docs/profiling.md](../../profiling.md); `SystemSet` opens a zone per
  system call already.
- `ThreadSafe<T>`: shared state between the main thread and workers.
- `libraryResourceFile`: how the libraries generated in the source tree are found from a build folder.

## Dependencies

Tracy (`lib/tracy`), taskflow (`profile_workers.h`), cereal (`quad_tree.h`), glm, EnTT. No other folder of `src/`.

## Design docs

[docs/profiling.md](../../profiling.md).
