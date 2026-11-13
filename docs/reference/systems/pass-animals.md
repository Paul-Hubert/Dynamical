# animals

`AnimalPass` in [`src/renderer/passes/animal_pass.cpp`](../../../src/renderer/passes/animal_pass.cpp) · Scope: Host · Needs renderer: yes
· Lanes: `renderer.passes` (`"animals"`) · Parameters: no

Draws every entity with a `Transform`, an `Animal` and an `AnimalPose`: the skinned meshes of the animal
library ([docs/animals.md](../../animals.md)), four joints a vertex, the skinning matrices of every animal
drawn in a frame in one storage buffer (set 2). One level of detail per animal and target, animals out of
the desktop view skipped, no shadow maps. With ray tracing it also skins the animals nearest to the viewers
with a compute shader and lists them in `RayAnimals`, from which `RayScene` builds their acceleration
structures (their shadows).

## Behaviour

**Constructor**:

- Ensures the `Animal`, `AnimalPose` and `Transform` storages exist, emplaces `AnimalStats`, gets the
  `AnimalLibrary` through `animalLibrary(reg)` (loads `resources/animals` and emplaces it when absent; info
  or warning in the log). One species slot per library entry.
- Pool (512 materials + one joint set per frame in flight), material set layout (**set 1**: 0 colour, 1
  normal, fragment), joint set layout (**set 2**: binding 0 storage buffer, vertex). A sampler with
  `RenderSettings::textures.animal_anisotropy` (8 without settings), white and flat normal fallbacks.
- Per frame in flight: a mapped `CPU_TO_GPU` storage buffer of `max_joints` (65536) matrices (with a
  device address under ray tracing) and its set.
- Layout: sets 0 (view), 1, 2; push constants `AnimalPC` (model, tint + height, alpha cutoff / normal map
  flag / first joint).
- With ray tracing: emplaces `RayAnimals`, creates the compute pipeline `animal_skin.rt.comp.glsl.spv`
  (push constants `SkinPC`: buffer addresses of the vertices, joints and output, first vertex, count, first
  joint, first output vertex).

**createPipeline(target, renderpass)**: `animal.vert.glsl.spv` + `sceneFragmentShader(ctx, "animal")`,
`AnimalVertex` input (position, normal, uv, four joints, four weights), back face culled and a two sided
copy.

**request(species)**: the species definition is the library's (`AnimalLibrary::species(i)`, already
loaded); without it the job fails at once. Else a `silent_async` task on the `tf::Executor` loads the
geometry (`AnimalLibrary::loadGeometry`) and decodes the colour and normal maps not loaded yet. At most 6
jobs at a time.

**finishJob(job)** (main thread): error → log and `Failed`. Uploads the new textures (a failed one is
remembered without image, warning), the height, centre and radius of the bounding sphere, the vertex buffer
(`rayInputUsage`); with ray tracing the shadow mesh: the indices of level 1 (or level 0 when it is the
only one) rebased on their lowest vertex, uploaded, its first vertex, vertex count, index count and a
shadow colour (0.3 x the first material's tint); the full index buffer; a material set per material (two
sided when the material says so or is alpha tested; no alpha test without a colour map). Logs an info line.

**update(frame_index)** (main thread, every frame):

1. Clears the instance lists, finishes the jobs that are done, resets `AnimalStats` and counts the species
   loaded / loading.
2. Eyes as the vegetation pass (desktop from `Camera` with the cull cone when
   `vegetation_params.cull`; headset from `VRRig` + `VRInput::head` when the session runs).
3. With the skin pipeline: the shadow range is the largest `lighting[target].traced_range x 1.2` of the
   active targets, the most casters the largest `lighting[target].animals` (none: no range).
4. For every `Transform` + `Animal` + `AnimalPose`: counts `animals`; requests an unloaded species; skips it
   while not `Ready`, when `RenderSettings::animals` is false, or when `AnimalPose::skin` does not have the
   species' joint count. Per active eye: culls by the sphere, picks the level (`force_lod`, else the
   first level whose `screen_height` the projected height x `lod_bias` reaches, else the last), copies the
   skin matrices once into this frame's joint buffer (stops with a warning past 65536) and adds an
   instance. An animal within the shadow range of a viewer (in view or not) with a shadow mesh becomes a
   caster (its joints copied when they are not yet).
5. With `RayAnimals`: clears its list; buffers of skinned vertices left `NUM_FRAMES + 2` frames ago are
   freed; keeps the nearest casters up to the most allowed; grows the `skinned` buffer (four floats a
   vertex, at least 65536 vertices, twice what is needed) when needed, the old one kept until the frames in
   flight are done; records into the compute command buffer of the frame (`Compute::getCommandBuffer`,
   GPU zone "Skin animals for the rays") one dispatch per caster, and appends a `RayAnimals::Animal` per
   caster (entity, the skinned buffer, its first vertex and count, the shadow index buffer and count, the
   matrix, the colour).
6. `joints`, `drawn` and `triangles` (desktop) in `AnimalStats`; instances sorted by species then level.

**record(command, view)** (both targets concurrently; reads its own lists and `RenderSettings`): binds set 0
and this frame's joint set (set 2), per instance its species' buffers, per draw of its level the pipeline
and material set when they change, pushes `AnimalPC` (with the first joint) and draws indexed.

**Destructor**: waits for the jobs, destroys pipelines, views, and with ray tracing waits for the device,
destroys the skin pipeline and erases `RayAnimals`.

## Parameters

None from the scene. Settings read:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `RenderSettings::animals` | bool | true | false: nothing drawn, no caster |
| `RenderSettings::vegetation_params.lod_bias`, `force_lod`, `cull` | float, int, bool | 2, -1, true | levels of detail and culling, shared with the plants |
| `RenderSettings::vegetation_params.normal_maps` | bool | true | normal maps (record) |
| `RenderSettings::lighting[target].traced_range` | float | 60 | casters within 1.2 x this of a viewer |
| `RenderSettings::lighting[target].animals` | int | 32 | most animals with a shadow |
| `RenderSettings::textures.animal_anisotropy` | int | 8 | sampler anisotropy (constructor) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| Animal | component | read | update | species |
| AnimalPose | component | read | update | `skin` matrices copied to the joint buffer |
| Transform | component | read | update | position, rotation, scale, matrix |
| AnimalStats | context | write+create | constructor (emplace), update | animals, drawn, triangles, joints, species loaded / loading |
| AnimalLibrary | context | read+create | constructor (`animalLibrary`), request, finishJob | species definitions and directory; loaded and emplaced when absent |
| RayAnimals | context | readwrite+create+destroy | constructor (emplace, ray tracing), update, destructor (erase) | the animals skinned for the rays this frame |
| RenderSettings | context | read | constructor, update, record | see Parameters |
| Camera | context | read | update | desktop eye |
| VRInput | context | read | update | headset running, head position |
| VRRig | context | read | update | rig transform |
| Executor | context | read | request | `tf::Executor` that loads the species |

## Side effects

- Loads species geometry and textures on taskflow workers; uploads buffers and textures, allocates descriptor sets.
- Writes the skinning matrices of the frame into a mapped buffer per frame in flight.
- With ray tracing records compute dispatches (`animal_skin.rt.comp.glsl`) into the frame's compute command buffer, and (re)allocates the skinned vertex buffer.
- Logs an info line per species loaded, errors and warnings for failed species or textures, a warning when more than 65536 joints are in view.

## Order and dependencies

- `AnimalAnimationSys` (`"AnimalAnimation"`) and `"AnimalGait"` write the `AnimalPose` before the renderer runs (systems tick before `Renderer::render`).
- Its `update()` records the skinning before `RayScene::update()` records the builds of the same compute command buffer, which reads `RayAnimals` and refits the casters' structures.
- `HerdSys`, `AnimalLineup`, `AnimalWalkers`, `AnimalAgentView` make the `Animal` entities. Captures read `AnimalStats`.

## Used in scenes
<!-- generated -->

## Tests

- `ANIMAL.*` in [`tests/unit/animals.cpp`](../../../tests/unit/animals.cpp) (the library, skinning, clips the pass draws).
- Shots by hand: `tests/capture/lighting_animals.capture.json` (scene `animals_test`, their shadows in the rays).
