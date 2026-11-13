# src/renderer/passes

The scene passes of the renderer: each draws one kind of thing into both render targets (desktop and
headset). A scene lists the ones it wants in `renderer.passes` (names in `scene_passes[]` of
[`src/scene/scene_def.h`](../../../src/scene/scene_def.h): `terrain`, `grass`, `vegetation`, `animals`,
`model`, `ui`); the `Renderer` always adds `SkyPass` first. Every pass derives from `ScenePass`
([`src/renderer/scene_pass.h`](../../../src/renderer/scene_pass.h)): `createPipeline()` once per target,
`update()` on the main thread once a frame (it may write the registry and the GPU), `record()` concurrently for
both targets (it only reads: `const entt::registry&`). Set 0 of every scene pipeline is `Context::view_layout`
(ViewUBO, tree shade, soil maps, lights, and with ray tracing the rays, the probes and the plant volumes).

## Files

| File | What |
|---|---|
| [`terrain_pass.h`](../../../src/renderer/passes/terrain_pass.h) / [`.cpp`](../../../src/renderer/passes/terrain_pass.cpp) | `TerrainPass`: the marching cubes chunks, ground textures, soil patches, relief rays; `terrainVertexBindings()` / `terrainVertexAttributes()` (shared with the grass) |
| [`grass_pass.h`](../../../src/renderer/passes/grass_pass.h) / [`.cpp`](../../../src/renderer/passes/grass_pass.cpp) | `GrassPass`: the grass shell, the baked rays of the ten kinds, species noise, height maps per chunk, the stencil see through |
| [`vegetation_pass.h`](../../../src/renderer/passes/vegetation_pass.h) / [`.cpp`](../../../src/renderer/passes/vegetation_pass.cpp) | `VegetationPass`: SpeedTree plants, species loaded on first use, levels of detail, far plants from their volumes, meshes to the rays |
| [`animal_pass.h`](../../../src/renderer/passes/animal_pass.h) / [`.cpp`](../../../src/renderer/passes/animal_pass.cpp) | `AnimalPass`: skinned animals, joint buffer per frame, compute skinning of the shadow casters (`RayAnimals`) |
| [`model_pass.h`](../../../src/renderer/passes/model_pass.h) / [`.cpp`](../../../src/renderer/passes/model_pass.cpp) | `ModelPass`: glTF models (`ModelC` + `ModelRenderable`), material sets, meshes to the rays |
| [`ui_pass.h`](../../../src/renderer/passes/ui_pass.h) / [`.cpp`](../../../src/renderer/passes/ui_pass.cpp) | `UIPass`: Dear ImGui, desktop only, overlay |
| [`sky_pass.h`](../../../src/renderer/passes/sky_pass.h) / [`.cpp`](../../../src/renderer/passes/sky_pass.cpp) | `SkyPass`: the sky, sun, moon and stars, one fullscreen triangle, always first |

## Passes

No registered system is defined here. The pages of the passes:

| Scene name | Class | Page |
|---|---|---|
| (always) | `SkyPass` | [pass-sky](../systems/pass-sky.md) |
| `terrain` | `TerrainPass` | [pass-terrain](../systems/pass-terrain.md) |
| `grass` | `GrassPass` | [pass-grass](../systems/pass-grass.md) |
| `vegetation` | `VegetationPass` | [pass-vegetation](../systems/pass-vegetation.md) |
| `animals` | `AnimalPass` | [pass-animals](../systems/pass-animals.md) |
| `model` | `ModelPass` | [pass-model](../systems/pass-model.md) |
| `ui` | `UIPass` | [pass-ui](../systems/pass-ui.md) |

## Components and context variables

None is defined here. The passes create or write context variables defined elsewhere:
[VegetationStats](../components/VegetationStats.md), [AnimalStats](../components/AnimalStats.md) (stats for the
debug menu and the captures), [RayPlantMeshes](../components/RayPlantMeshes.md), [RayAnimals](../components/RayAnimals.md),
[RayMeshes](../components/RayMeshes.md) (with ray tracing, defined in [src/renderer/lighting](src-renderer-lighting.md)),
and set `RenderSettings::grass_pass` ([RenderSettings](../components/RenderSettings.md)).

## Worth knowing

- Draw order is the order of the scene's list, after the sky; the usual one is `terrain, grass, vegetation, animals, model, ui`. The grass needs the terrain before it (depth and stencil).
- `vegetation` and `animals` load species lazily on the `tf::Executor` (`silent_async`, 6 jobs at a time) and finish them on the main thread in `update()`; their destructors wait for running jobs.
- With ray tracing (`ctx.device.ray.enabled()`): fragment shaders are the `.rt.spv` variants (`sceneFragmentShader`), the scene passes draw two colour attachments (colour and sun of `SunVisibility`), and the passes hand meshes to the rays (`RayMeshes`).
- Pipelines differ between targets (colour formats); the UI pass is `overlay()` and draws in the final render pass.

## Dependencies

- [src/renderer/context](src-renderer-context.md) (`Context`, device, transfer, compute, view layout), `src/renderer/util` (pipelines, textures, VMA wrappers), [src/renderer/terrain](src-renderer-terrain.md) (`TerrainBuffers`, `ChunkGPU`, `ChunkReady`, `ChunkLayers`, `SoilLayers`), [src/renderer/lighting](src-renderer-lighting.md) (`RayMeshes`, `RayPlantMeshes`, `RayAnimals`, `PlantVolumeSet`), `src/renderer/model` (`ModelC`).
- `src/logic/grass` (bake, textures, species, height), `src/logic/terrain` (chunks, ground relief), `src/logic/vegetation` (`Plant`, library), `src/logic/animals` (`Animal`, `AnimalPose`, library), `src/logic/components` (`RenderSettings`, `Transform`, `Camera`, VR), `src/logic/settings` (`Settings`), `src/view/components` (`ModelRenderable`), ImGui.

## Design docs

[docs/ground.md](../../ground.md), [docs/ground-relief.md](../../ground-relief.md), [docs/grass.md](../../grass.md) and [docs/grass/](../../grass/plan.md), [docs/vegetation.md](../../vegetation.md), [docs/forest.md](../../forest.md), [docs/animals.md](../../animals.md), [docs/lighting.md](../../lighting.md), [docs/graphics.md](../../graphics.md), [docs/profiling.md](../../profiling.md) (pass zones).
