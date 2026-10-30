# Archived : 2D tile map simulation

Archived on the `port/mc-grass-gltf-vr` branch when the marching cubes world, the raymarched grass,
glTF models and OpenXR rendering were brought back. Paths below mirror their original location under `src/`.
Nothing here is compiled (CMake only globs `src/`).

| What | Files |
|---|---|
| Tile map rendering | `logic/systems/map_render.*`, `map_upload.*`, `components/map_upload_data.h`, `shaders/maprender.*` |
| Heightmap / tile world | `logic/map/*` (MapManager, MapGenerator, chunks of tiles), `systems/chunk_generation.cpp`, `map_configurator.cpp`, `map_editor.cpp`, `water_flow.*`, `settings/map_configuration.*` |
| Particles (SPH) | `logic/systems/particle_simulation.*`, `shaders/particle.comp.glsl` |
| Sprite objects | `logic/systems/object_render.*`, `shaders/objectrender.*`, `components/object.h`, `renderable.h`, `util/color.h` |
| AI and gameplay on the tile map | `ai/*`, `systems/pather.cpp`, `basic_needs.cpp`, `harvest.cpp`, `eat.cpp`, `action_bar.cpp`, `selection.*`, `devmenu.cpp`, `factories/*` and their components |
| Top-down camera | `logic/systems/camera.cpp`, `components/camera.h` |

Everything used `Position` (managed by `MapManager`) and the EnTT 3.8 API (`reg.ctx<T>()`, `reg.set<T>()`).
To bring a part back : move it under `src/`, port the EnTT calls (`reg.ctx().get<T>()`, `reg.ctx().emplace<T>()`,
`reg.all_of<T>()`) and decide how it maps onto the marching cubes terrain (`logic/terrain/`).

The full history is in git, e.g. `git log --follow archive/tilemap/src/logic/map/map_manager.cpp`.
