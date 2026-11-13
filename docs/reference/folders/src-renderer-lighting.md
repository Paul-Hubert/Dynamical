# src/renderer/lighting

The GPU side of the lighting ([docs/lighting.md](../../lighting.md), [docs/lighting/built.md](../../lighting/built.md)):
the lights uniform of every frame, and with ray tracing (`ctx.device.ray.enabled()`) the acceleration
structures the rays see, the irradiance probes, the baked volumes of the plants and the per pixel visibility
of the sun of each render target. The `Renderer` owns `Lights` (always), `RayScene`, `ProbeVolume` and
`PlantVolumes` (ray tracing only); `ClassicRender` and `VRRender` own a `SunVisibility` each. Their per frame
order inside `Renderer::render`: `Lights::update` → the passes' `update` → `PlantVolumes::prepare` →
`RayScene::update` → `ProbeVolume::update` → `PlantVolumes::update` → the targets' `updateView`
(`SunVisibility::beginFrame`) → recording (`SunVisibility` scene / trace / composite).

## Files

| File | What |
|---|---|
| [`lights.h`](../../../src/renderer/lighting/lights.h) / [`.cpp`](../../../src/renderer/lighting/lights.cpp) | `LightsUBO` (set 0 binding 3), `Lights` (fills it from `Sky` and the `PointLight`s), `lightingView()` (the `ViewUBO::lighting` of a target) |
| [`ray_meshes.h`](../../../src/renderer/lighting/ray_meshes.h) | What the passes hand to the rays: `RayMesh`, `RayBox`, the context variables `RayMeshes`, `RayBake`, `RayPlantMeshes`, `PlantVolumeSet`, `RayAnimals`, the plant components `PlantInRays`, `PlantRayTransforms` |
| [`ray_scene.h`](../../../src/renderer/lighting/ray_scene.h) / [`.cpp`](../../../src/renderer/lighting/ray_scene.cpp) | `RayScene`: bottom level structures of chunks, meshes and animals, the unit box, the geometry table, the top level structure every frame; tag `ChunkLit`; `Accel` |
| [`probe_volume.h`](../../../src/renderer/lighting/probe_volume.h) / [`.cpp`](../../../src/renderer/lighting/probe_volume.cpp) | `ProbeVolume`: bricks of 65³ probes per lit chunk in pages, the ray budget of each frame, `probe_update.rt.comp.glsl` |
| [`plant_volumes.h`](../../../src/renderer/lighting/plant_volumes.h) / [`.cpp`](../../../src/renderer/lighting/plant_volumes.cpp) | `PlantVolumes`: per species density / colour / normal 3D textures baked against the rays (`plant_bake.rt.comp.glsl`) |
| [`sun_visibility.h`](../../../src/renderer/lighting/sun_visibility.h) / [`.cpp`](../../../src/renderer/lighting/sun_visibility.cpp) | `SunVisibility`: the scene render pass with colour and sun apart, the traced and filtered visibility of the sun, the composite |

## Services

No registered system is defined here. The service pages:
[Lights](../systems/service-Lights.md), [RayScene](../systems/service-RayScene.md),
[ProbeVolume](../systems/service-ProbeVolume.md), [PlantVolumes](../systems/service-PlantVolumes.md),
[SunVisibility](../systems/service-SunVisibility.md) (touches no registry data).

## Components and context variables

| Name | Kind | Defined in | Written by |
|---|---|---|---|
| [RayMeshes](../components/RayMeshes.md) | context | `ray_meshes.h` | RayScene (create), VegetationPass, ModelPass, PlantVolumes |
| [RayBake](../components/RayBake.md) | context | `ray_meshes.h` | RayScene (create, `placed`), PlantVolumes |
| [RayPlantMeshes](../components/RayPlantMeshes.md) | context | `ray_meshes.h` | VegetationPass (create), PlantVolumes (`box`) |
| [PlantVolumeSet](../components/PlantVolumeSet.md) | context | `ray_meshes.h` | PlantVolumes |
| [RayAnimals](../components/RayAnimals.md) | context | `ray_meshes.h` | AnimalPass |
| [PlantInRays](../components/PlantInRays.md) | tag | `ray_meshes.h` | RayScene |
| [PlantRayTransforms](../components/PlantRayTransforms.md) | component | `ray_meshes.h` | RayScene |
| [ChunkLit](../components/ChunkLit.md) | tag | `ray_scene.h` | RayScene |

`LightingStats` (the stats every service here writes) is defined in `src/logic/components/lighting_stats.h`.

## Worth knowing

- Everything here but `Lights` exists only with ray tracing; `ray_tracing=false` forces the fallback (the reference of the goldens).
- The services reach the shaders through `Context`: `tlas`, `ray_geometries`, `ray_textures` (RayScene), `probe_index_view`, `probe_light`, `probe_direct` (ProbeVolume), `plant_volumes` (PlantVolumes), `lights_buffers` (Lights); `Context::writeViewSet` writes them into every view set (bindings 3 to 12). Texture arrays only grow.
- GPU objects that frames in flight may still use are retired and destroyed `NUM_FRAMES + 1` frames later.
- All the GPU work of the lighting is recorded into the frame's compute command buffer, after the marching cubes and the animal skinning, before the graphics submit.

## Dependencies

- [src/renderer/context](src-renderer-context.md) (`Context`, device, compute, transfer), `src/renderer/util`, [src/renderer/terrain](src-renderer-terrain.md) (`TerrainBuffers`, `ChunkTriangles`, `ChunkGPU`, `ChunkReady`), `src/renderer/model` (`ModelC`), `src/renderer/tree_shade.h`, `src/renderer/view_ubo.h`.
- `src/logic/lighting` (`ProbeBricks`, `classifyProbes`, `ProbeLight`), `src/logic/sky` (`Sky`, `skyLight`), `src/logic/components` (`LightingStats`, `RenderSettings`, `PointLight`, `Transform`, `Camera`, VR, `RigidBody`), `src/logic/terrain` (chunks), `src/logic/vegetation` (`Plant`, `VegetationLibrary`), `src/view/components` (`ModelRenderable`).

## Design docs

[docs/lighting.md](../../lighting.md), [docs/lighting/built.md](../../lighting/built.md), [docs/vegetation.md](../../vegetation.md) (far plants), [docs/animals.md](../../animals.md), [docs/graphics.md](../../graphics.md) (tiers), [docs/profiling.md](../../profiling.md) (GPU zones).
