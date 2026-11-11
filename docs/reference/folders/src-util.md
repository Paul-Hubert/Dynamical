# `src/util/`

> [Source](../../../src/util/) · Parent: [`src/`](src.md)

## Summary
Small engine-wide helpers compiled into `dynamical_sim` (`.cpp` files) or header-only: logging with an optional file tee, executable path lookup, file reading, generated capsule/sphere meshes, and a few legacy templates. Nothing here is stored in a registry or touches one.

## Files
| File | Contents |
|---|---|
| `log.h/.cpp` | `Level { trace, debug, info, warning, error, critical }`; `log(level)` returns `std::cout` for trace/debug/info and `std::cerr` for warning and above, or, after `setLogFile(path)`, streams teeing to the console and to the file (opened in append mode, parent directories created, flushed at every newline). Used for dynamical_train's `-logFile` |
| `paths.h/.cpp` | `executableDirectory()`: `GetModuleFileNameW` on Windows (growing buffer), `/proc/self/exe` elsewhere; falls back to the current path |
| `util.h/.cpp` | `readFile(path)` (whole binary file; prints and returns empty on failure), `nTimes`, `makeSet`, `removeElement` (both take the vector by value, so they only return a count), `sq`, `s_sq`, `c_sq`, `c_pow`, `ThreadSafe<T>` (mutex-guarded value, `operator*` returns a locking `Locker`), `lerp` |
| `primitive_mesh.h/.cpp` | `MeshData { positions, normals, uvs, indices }` (CCW outward); `capsuleMesh(radius, half_height, segments = 24, cap_rings = 8)` along local Y like Jolt's `CapsuleShape`; `sphereMesh(radius, segments = 24, rings = 16)`. Used by `ModelManager` and `WorldView` |
| `entt_util.h` | `Alias<hash, T>` and `Entity<hash>`: wrappers giving distinct types to values or entities keyed by a hashed string (included by `view/game.cpp`) |
| `profile.h` | `profiling` (true without `NDEBUG`), `marchingcubesprofiling`, `systemprofiling` constants |
| `quad_tree.h` | `QuadTree<T>`: legacy sparse 8-way tree with cereal serialisation, growing its root to fit; not included by current sources |
| `tiled_noise.h` | `TiledNoise`: Perlin noise with a 256-entry permutation (seeded `std::default_random_engine`), `noise`, `octaveNoise`; used by `renderer/passes/grass_pass.cpp` |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
