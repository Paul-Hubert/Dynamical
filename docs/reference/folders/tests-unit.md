# `tests/unit/`

> [Source](../../../tests/unit/) · Parent: [`tests/`](tests.md)

## Summary
doctest cases of suites `unit` and `scenario` that need neither gRPC nor ONNX Runtime: scene loading, the system set's lanes and phases, worlds and their determinism, physics behaviour, the sphere roller task, argument parsing, mesh generation, image diffing and the library header boundary.

## Files
| File | Contents |
|---|---|
| `smoke.cpp` | T0.1: doctest runs |
| `header_boundary.cpp` | T1.1: `dynamical_sim` sources name no vulkan, SDL, imgui, openxr or `renderer/` |
| `scene_loader.cpp` | T1.3 malformed scenes report file and JSON path (fixtures in `tests/data/scene/resources/scenes/bad/`); T1.4 variables, command-line overrides, typed substitution, file references, resolved round trip; T1.5 per-world variables; host order, world systems, prefabs, refusals |
| `base_scene.cpp` | T2.4: the `base` scene's host system list (with view-system stubs) |
| `system_set.cpp` | T1.6: Act only at k = 0, Observe only at k = period - 1, parked worlds run only Observe, `fixed_observe_only`; registry names kept |
| `world.cpp` | Worlds take clock, phases, physics and seeds from the template; `WorldRng` streams repeat per seed and stay in range |
| `test_boxes.cpp` | S2.1 boxes come to rest after 600 ticks; S2.2 determinism with 1 and 8 threads; S2.3 world isolation by seed |
| `friction_combine.cpp` | S2.4: max vs min friction combine on a 30 degree incline (`incline.scene.json`) |
| `jolt_runtime.cpp` | T2.1 no leaked allocations over 100 sequential and 8 concurrent worlds; T2.2 `threads=1` uses `JobSystemSingleThreaded`; collision layers |
| `sphere_roller.cpp` | SR.* sphere roller tests: goal frame, observations, rewards, episodes, heuristic vs zero action on `sphere_roll` / `sphere_roll_hills` |
| `run_args.cpp` | `parseRunArgs`: keys, variable order, fractions, capture/probe defaults, unknown keys and bad values |
| `primitive_mesh.cpp` | T2.5: capsule vertices lie on the capsule surface |
| `imgdiff.cpp` | `tools/imgdiff`: identical images, size mismatch, MAE threshold, strong-pixel fraction, heatmap |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
