# PlantVolumes

`PlantVolumes` in [`src/renderer/lighting/plant_volumes.cpp`](../../../src/renderer/lighting/plant_volumes.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by the `Renderer`, only with ray tracing) · Parameters: no

The baked volumes of the plants ([docs/lighting.md](../../lighting.md), "Plant volumes"): per species with a
traced mesh and at least 2 m tall, three 3D textures over the bounds of its most detailed level (at most 64 x
128 x 64 texels, up to 4 levels): density, colour, normal + sky seen. Baked on the GPU a few slices a frame
by `plant_bake.rt.comp.glsl`, which traces the species alone, placed far under the world (`RayBake`). Far
from the viewers a plant is then a box in the rays (`RayBox` in `RayMeshes`, `RayPlantMeshes::box`) and
`VegetationPass` draws it from the same textures (`PlantVolumeSet`). Nothing is kept on disk.

## Behaviour

**Constructor**: emplaces `PlantVolumeSet`; the set layout of the bake (three storage images), a pool of 128
sets, the pipeline (set 0 the view layout, set 1 the images, push constants `BakePC`), a linear clamped
sampler; `Context::plant_volumes` points at its image infos (bindings 10 to 12 of the view sets).

**prepare()** (main thread, before `RayScene::update()`): without `RayPlantMeshes` nothing. Else, among the
species with a mesh and bounds not done: when nothing is baking, `start(species)` begins the first one (a
species under 2 m or past 128 volumes is done for good, without volume); the others are counted as waiting.
Writes `RayBake::mesh` (the baking species' mesh, -1 none) and `origin` (0, -20000, 0).

**start(species)**: texel size from the bounds (`RayPlantMeshes::min`, `max`; at least 3 cm), size, levels;
the fill colour of empty texels from the parts of its `RayMeshes` mesh (mean of the leaves, else of all);
three images in the general layout, views of every level and of the first, the bake set.

**update(view_set)** (after `ProbeVolume::update()`, with its view set): adds the pending species to
`LightingStats::structures_pending`; when a species is baking and `RayBake::placed` (RayScene put it in this
frame's top level structure), records one dispatch of up to `RenderSettings::streaming.plant_volume_slices`
(8) slices (GPU zone "Bake plant volume") and a barrier; on the last slice `finish()`.

**finish(command)**: blits the smaller levels; the volume's index is the next of bindings 10 to 12 (the infos
grow); adds its `RayBox` to `RayMeshes` and stores the index in `RayPlantMeshes::box[species]`; writes
`PlantVolumeSet::species[species]` (index, min, size, voxel); logs an info line with the species id
(`VegetationLibrary`).

**Destructor**: waits for the device, clears `Context::plant_volumes`, erases `PlantVolumeSet`, destroys
views, sampler, pipeline, layouts, pool.

## Parameters

None from the scene. Settings read: `RenderSettings::streaming.plant_volume_slices` (int, 8): slices baked a
frame.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| PlantVolumeSet | context | readwrite+create+destroy | constructor (emplace), finish, destructor (erase) | baked volume of each species, for the vegetation pass |
| RayPlantMeshes | context | readwrite | prepare, start, finish | species meshes and bounds read; `box` written |
| RayMeshes | context | readwrite | start, finish | parts of a species' mesh read; the volume's `RayBox` added |
| RayBake | context | readwrite | prepare, update | asks for the baking species in the scene; reads `placed` |
| LightingStats | context | readwrite | update | `structures_pending +=` species still to bake |
| RenderSettings | context | read | update | `streaming.plant_volume_slices` |
| VegetationLibrary | context | read | finish | species id for the log |

## Side effects

- Allocates three 3D images per baked species (GPU only), records compute dispatches and blits into the frame's compute command buffer.
- Writes `Context::plant_volumes`, read by `Context::writeViewSet` (bindings 10 to 12, only ever added).
- Logs an info line per volume baked; the destructor waits for the device.

## Order and dependencies

- `VegetationPass` fills `RayPlantMeshes` (it exists only when the scene has the vegetation pass); `RayScene` builds the species' structure and places the bake mesh (`RayBake::placed`) between `prepare()` and `update()`.
- `update()` needs the view set of `ProbeVolume` (the `Renderer` passes `probe_volume->viewSet()`).

## Used in scenes
<!-- generated -->

## Tests

- No unit test. Shots by hand: `tests/capture/lighting_volumes.capture.json` (scene `vegetation_test`).
