# vegetation

`VegetationPass` in [`src/renderer/passes/vegetation_pass.cpp`](../../../src/renderer/passes/vegetation_pass.cpp) · Scope: Host · Needs renderer: yes
· Lanes: `renderer.passes` (`"vegetation"`) · Parameters: no

Draws every entity with a `Transform` and a `Plant`: the SpeedTree geometry of the vegetation library, one
level of detail per plant and target, alpha tested leaves lit from both sides, billboard cards in the
distance, and with ray tracing the plants far away from the baked volume of their species in place of their
billboard ([docs/vegetation.md](../../vegetation.md), [docs/lighting.md](../../lighting.md)). A species is
loaded the first time a plant uses it (or `VegetationPreload` asks): files and mips on the taskflow
executor, Vulkan objects on the main thread. With ray tracing it also gives each species' most detailed
level to the rays (`RayMeshes`, `RayPlantMeshes`).

## Behaviour

**Constructor**:

- Ensures the `Plant` and `Transform` storages exist (so the views work from the recording threads),
  emplaces `VegetationStats`, and gets the `VegetationLibrary` through `vegetationLibrary(reg)`, which
  loads it from `resources/vegetation` (build folder, else the source tree) and emplaces it when it is not
  there yet (info or warning in the log). One `Species` slot per library species, all null.
- With ray tracing (`RayMeshes` present, emplaced by `RayScene` before the passes): emplaces
  `RayPlantMeshes` with `mesh` = -1 for every species.
- Pool for 1024 materials, material set layout (**set 1**: 0 colour, 1 normal, 2 extra (roughness,
  metallic, occlusion), 3 subsurface; fragment stage), a sampler (anisotropy
  `RenderSettings::textures.vegetation_anisotropy`, 8 without settings) and an atlas sampler (mips stop at
  level 5, bias -0.5) for cut out leaf atlases, four 1x1 fallback textures.
- Layouts: `layout` (set 0 view, set 1 material, push constants `PlantPC`: model matrix, tint + height,
  alpha cutoff / billboard / normal map / subsurface flags) and `volume_layout` (set 0 only, push
  constants `VolumePC`: model, volume min + index, size + texel size).

**createPipeline(target, renderpass)**: `vegetation.vert.glsl.spv` + `sceneFragmentShader(ctx,
"vegetation")`, back face culled and a two sided copy; with ray tracing a third, `vegetation_volume.vert` +
`vegetation_volume.rt.frag` (36 vertices of a box, no vertex buffer, no culling).

**request(species)** (from `update`): creates the species (`Loading`) and a `Job`, copies the library into a
`shared_ptr`, and runs a `silent_async` task on the `tf::Executor`: `loadSpecies`, `loadGeometry`, then every
texture of its materials not loaded yet is decoded with its mips (`decodeTextureMips`, alpha cutoff on the
colour maps). Errors are kept in the job. At most 6 jobs at a time.

**finishJob(job)** (from `update`, main thread): logs an error and marks the species `Failed` on a job
error; uploads each new texture (sRGB or UNORM; a failed one is remembered without image, with a warning),
its mean colour from the smallest mip; computes the height and the bounding sphere; uploads the vertex and
index buffers (with `Context::rayInputUsage()`); allocates and writes one material set per material (the
fallbacks where a map is missing). With `RayMeshes` and a first level that is not a billboard: a
`RayMesh` of kind `Plant` from level 0, a part per draw (colour = mean colour x tint; cut out parts get the
texture index from `RayMeshes::texture`, the alpha cutoff and a transmission colour), added to
`RayMeshes`; `RayPlantMeshes::mesh`, `min`, `max` of the species are written. Logs an info line.

**update(frame_index)** (main thread, every frame):

1. Clears the instance lists of both targets; finishes the jobs that are done.
2. Resets `VegetationStats`; requests the species of `VegetationPreload::species` not loaded yet (counts
   those that wait for a job slot in `species_loading`); counts `species_loaded` and `species_loading`.
3. Returns when `RenderSettings::vegetation` is false.
4. The eyes: desktop from the `Camera` context variable (when its pipeline exists), with a cull cone around
   the corners of a 21:9 window when `vegetation_params.cull`; headset from `VRRig` x `VRInput::head`
   when `VRInput::session_running` (tan of half the field 1.2, no culling).
5. For every `Transform` + `Plant`: counts `plants`; requests an unloaded species (or flags one waiting);
   skips species not `Ready`. Per active eye, culls by the sphere, picks the level: `Plant::lod ==
   Plant::volume` takes the last level, `force_lod` or `Plant::lod` forces one (clamped), else the first
   level whose `screen_height` the plant's projected height x `lod_bias` reaches; none: not drawn. A
   billboard level with `far_plants_on_screen` (and no forced level), or `Plant::volume`, goes to the
   volume list when `PlantVolumeSet` has a baked volume for the species and the volume pipeline exists;
   else to the instance list.
6. Sorts the instances by species then level; `drawn` and `triangles` count the desktop's.

**record(command, view)** (both targets concurrently; reads its own lists, `RenderSettings` and
`PlantVolumeSet`): binds set 0, then per instance the vertex and index buffers of its species, per draw of
its level the pipeline (two sided or not) and material set when they change, pushes `PlantPC` and draws
indexed. Then the volume boxes: binds the volume pipeline and set 0, pushes `VolumePC` (from
`PlantVolumeSet::species[s]`) and draws 36 vertices each.

**Destructor**: waits for the running jobs, then destroys pipelines, layouts, views, samplers, pool.

## Parameters

None from the scene. Settings read:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `RenderSettings::vegetation` | bool | true | false: no instance (species still load) |
| `RenderSettings::vegetation_params.lod_bias` | float | 2 | > 1 keeps the detailed levels further |
| `RenderSettings::vegetation_params.force_lod` | int | -1 | >= 0: every plant at that level |
| `RenderSettings::vegetation_params.cull` | bool | true | cull plants out of the desktop view |
| `RenderSettings::vegetation_params.normal_maps`, `subsurface` | bool | true | shader flags (record) |
| `RenderSettings::far_plants_on_screen` | bool | true | far plants drawn from their volume (`far_plants=` on the command line) |
| `RenderSettings::textures.vegetation_anisotropy` | int | 8 | sampler anisotropy (constructor) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| Plant | component | read | update | species, forced level (`lod`, `Plant::volume`) |
| Transform | component | read | update | position, scale, matrix of each plant |
| VegetationStats | context | write+create | constructor (emplace), update | plants, drawn, triangles, species loaded / loading |
| VegetationLibrary | context | read+create | constructor (`vegetationLibrary`), request, finishJob | species list, files; loaded and emplaced when absent |
| VegetationPreload | context | read | update | species to load before any plant uses them |
| RayMeshes | context | readwrite | constructor (presence), finishJob | adds each species' traced mesh and leaf textures |
| RayPlantMeshes | context | readwrite+create | constructor (emplace), finishJob | mesh index and bounds of each species |
| PlantVolumeSet | context | read | update, record | which species have a baked volume, its bounds and index |
| RenderSettings | context | read | constructor, update, record | see Parameters |
| Camera | context | read | update | desktop eye: position, field, forward |
| VRInput | context | read | update | headset running, head position |
| VRRig | context | read | update | rig transform of the headset eye |
| Executor | context | read | request | `tf::Executor` that loads the species |

## Side effects

- Loads species files and textures from the vegetation library on taskflow workers (`silent_async`, 6 at most at a time); uploads vertex / index buffers and textures (transfer queue), allocates descriptor sets.
- Logs an info line per species loaded, an error per species that fails, a warning per texture that fails.
- With ray tracing, gives meshes and leaf textures to the rays (`RayMeshes`), which `RayScene` builds acceleration structures from.
- Records the draws of both targets; the destructor blocks until the running jobs end.

## Order and dependencies

- `RayScene` must be built before the passes for `RayPlantMeshes` to exist (the `Renderer` does so).
- Its `update()` runs before `PlantVolumes::prepare()` and `RayScene::update()`: a species finished this frame is in the rays of this frame's builds.
- `ForestSys`, `VegetationScatter`, `VegetationLineup` (systems) make the `Plant` entities; `VegetationPreload` is filled by `ForestSys`. Captures wait for `VegetationStats::species_loading == 0`.

## Used in scenes
<!-- generated -->

## Tests

- `VEG.*` in [`tests/unit/vegetation.cpp`](../../../tests/unit/vegetation.cpp) (the library and geometry it loads), `FOREST.*` in [`tests/unit/forest.cpp`](../../../tests/unit/forest.cpp).
- Shots by hand: `tests/capture/lighting.capture.json` (scene `forest`), `lighting_volumes.capture.json` (scene `vegetation_test`, far plants from their volumes).
