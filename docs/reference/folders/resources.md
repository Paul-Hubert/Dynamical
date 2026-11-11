# `resources/`

> [Source](../../../resources/) · Parent: repository root

## Summary
Runtime data. The whole directory is copied to `<build>/resources` (dynamical loads from `./resources`, and `SceneLoadOptions::resources` defaults to `"resources"`); compiled SPIR-V shaders are added there as `resources/shaders/*.spv` by the build. For `dynamical_train`, only `resources/{scenes,prefabs,agents}` (those that exist) are copied next to the executable, where `train_main.cpp` looks for them (`executableDirectory()/resources`).

## Files
| File | Contents |
|---|---|
| `scenes/*.scene.json` | scenes for `loadScene` (keys in [`src/scene/`](src-scene.md)). Rendered (dynamical, include `common/desktop.json`): `base` (marching-cubes world, host systems only, passes terrain/grass/model/ui, VR on), `test_boxes_view`, `sphere_view`, `sphere_view_heuristic`, `wheel_view`, `vr_roller` (two `sphere_world` worlds, VR and desktop players). Headless (dynamical_train / tests): `null` (variables `agents`, `episode_decisions` for `mode=null`, no worlds), `test_boxes` (`box_world` x `num_worlds`), `sphere_roll`, `sphere_roll_hills`, `wheel_roll` |
| `scenes/common/*.json` | include files: `desktop.json` (host pre `Input, UI, PlayerControl, DebugMenu`), `box_world.json` (template `box_world`), `sphere_world.json` (template `sphere_world`: 60 Hz physics, 15 Hz control, marching-cubes `WorldTerrain`, sphere agent), `sphere_world_hills.json` (template `sphere_world_hills`, height field), `wheel_body.json` (variables only: the wheel body overriding the sphere's) |
| `prefabs/box.json` | `Transform`, dynamic box `RigidBody` (half extent 0.5), `ModelRef models/box.gltf` |
| `prefabs/ground_flat.json` | `Ground` 150 x 10 x 150, top at 0, friction 1, restitution 0 |
| `models/box.gltf`, `models/primitives/{box,sphere}.gltf` | glTF models; the primitives (written by `tools/make_primitives.py`) are the unit shapes `WorldView` scales, kept out of `models/` so `ModelLineup` does not list them |
| `policies/{sphere_roller,wheel_roller}.onnx` | trained policies for the view scenes (`ModelWatcher` / `Policy`) |
| `textures/grass.png`, `textures/rock.jpg` | terrain textures |
| `mclookup.bin` | marching-cubes lookup table (read by `TerrainSys` and `logic/terrain/marching_cubes.cpp`) |
| `raycast_data.bin` | grass lookup texture baked by `tools/raycast` |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
`scenes/` (with `common/`), `prefabs/`, `models/`, `policies/`, `textures/` (described above; no separate docs). No `agents/` directory exists at this commit.
