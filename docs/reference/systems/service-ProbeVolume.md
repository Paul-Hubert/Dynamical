# ProbeVolume

`ProbeVolume` in [`src/renderer/lighting/probe_volume.cpp`](../../../src/renderer/lighting/probe_volume.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by the `Renderer`, only with ray tracing) · Parameters: no

The irradiance probes on the GPU ([docs/lighting.md](../../lighting.md), "Probes"): a brick of 65³ probes
per chunk in the rays that has a surface, one every 4 m, in pages of 8 bricks (at most 8 pages). Per probe,
the light from all around as two bands of spherical harmonics per colour (three RGBA16F 3D textures) and how
much of the sun and the sky it sees (RGBA8). Every frame a ray budget is spent on the probes that are traced
(the air near the ground or a plant) in the compute command buffer of the frame. The scene shaders read them
through the view set (bindings 7 to 9, `include/probes.glsl`). The CPU side (which probe is traced, the
brick table) is `logic/lighting/probes.*`.

## Behaviour

**Constructor**: an empty brick table (3D R16 image, `ProbeBricks::wrap`³), its nearest sampler, a linear
clamped sampler for the light; set layouts (page: five storage images; work: the list of probes and what the
rays found); the compute pipeline `probe_update.rt.comp.glsl.spv` (sets 0 view, 1 page, 2 work; push
constants `ProbePC`); per frame in flight a mapped ViewUBO, a mapped list of 2^18 probes and their sets.
Writes `Context::probe_index_view`, `probe_index_sampler`, `probe_light`, `probe_direct`.

**update(time)** (main thread, after `RayScene::update()`):

1. The viewer: the `Camera` position, or the headset (`VRRig` x `VRInput::head`) when its session runs.
2. `syncBricks(viewer)`: frees the bricks of chunks no longer valid, lit (`ChunkLit`), with a
   `ChunkVolume`, and not `ChunkDestroying`. Among chunks with `ChunkC` + `ChunkVolume` + `ChunkLit`
   without `ChunkDestroying` and without a brick, the nearest gets one (when full, the furthest brick goes
   if it is a chunk length further); new pages are created as needed (logged). Its probes are classified
   (`classifyProbes`, from `ChunkVolume::values` and the plants over 1.5 m around the chunk: `Transform` +
   `Plant` with the species height and radius of `VegetationLibrary`) and the classes uploaded. The table
   is uploaded when something changed. `LightingStats::bricks`, `probes_pending` (chunks still waiting
   while room is left).
3. Writes the frame's ViewUBO of the dispatches (the viewer, `time`, `treeShadeView`: the tree shade
   strength of `RenderSettings`) and the frame's view set (`Context::writeViewSet`).
4. Rays per probe `RenderSettings::rays_per_probe` (clamped 8..256), budget
   `probe_rays_per_frame / rays` probes (clamped to 4 M rays); grows the buffer of what the rays find
   (waits for the device).
5. `probes_active`, and `probes_pending` += the probes of never swept bricks.
6. Records, in the compute command buffer (GPU zone "Probes"): new bricks `First` (today's look at once);
   probes within the radius of a `PointLight` (+ `Transform`, intensity and radius > 0) marked hot and traced
   every frame for 30 frames after (keep 0.7); bricks never swept, nearest first, all the rays they can take;
   then the others in turn by a schedule that comes sooner for near bricks, a quarter of their probes each
   (`Trace` + `Gather` keep 0.85, `Fill` for the probes in the ground); the open air of one brick a frame
   (`Open`). A barrier after each dispatch and one for every later stage.

**viewSet()**: the frame's view set (`PlantVolumes::update` binds it).

**Destructor**: waits for the device, clears the `Context` pointers, destroys views, pipeline, pool,
layouts, samplers.

## Parameters

None from the scene. Settings read:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `RenderSettings::rays_per_probe` | int | 48 | rays a probe traced takes |
| `RenderSettings::probe_rays_per_frame` | int | 250000 | ray budget a frame (`probe_rays=` on the command line) |
| `RenderSettings::tree_shade` | float | 0.85 | strength of the plant shade in the dispatches' ViewUBO (`treeShadeView`) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| ChunkC | component | read | syncBricks | chunk position, centre, origin |
| ChunkVolume | component | read | syncBricks | densities the probes are classified from |
| ChunkLit | tag | read | syncBricks | only chunks in the rays get a brick |
| ChunkDestroying | tag | read | syncBricks | going chunks lose their brick |
| Plant | component | read | syncBricks | plants around a new brick |
| Transform | component | read | syncBricks, update | plant positions; torch positions |
| PointLight | component | read | update | torches: their probes traced every frame |
| VegetationLibrary | context | read | syncBricks | species height and radius |
| LightingStats | context | readwrite | syncBricks, update | bricks, probes pending, probes active |
| RenderSettings | context | read | update (and `treeShadeView`) | see Parameters |
| Camera | context | read | update | viewer |
| VRInput | context | read | update | headset running, head position |
| VRRig | context | read | update | rig transform |

## Side effects

- Allocates pages of 3D textures (61 MB each, up to 8; an info line each), uploads the brick table and the classes (transfer queue).
- Records compute dispatches into the frame's compute command buffer; writes the work list of the frame into a mapped buffer.
- Writes the `Context` probe pointers, read by `Context::writeViewSet` for every view set.
- `reserveFound` and the destructor call `waitIdle` on the device.

## Order and dependencies

- After `RayScene::update()`: it traces the top level structure built in the same compute command buffer, and `ChunkLit` is RayScene's.
- After `ChunkGeneratorSys` (`ChunkVolume`), `ForestSys` (plants), `TorchSys` (torches move).
- Before `PlantVolumes::update()`, which uses `viewSet()`.

## Used in scenes
<!-- generated -->

## Tests

- `LIGHT.5` to `LIGHT.7` in [`tests/unit/lighting.cpp`](../../../tests/unit/lighting.cpp): the probe light, `classifyProbes`, `ProbeBricks` (the CPU sides it uses). Shots by hand: `tests/capture/lighting.capture.json`, `lighting_torch.capture.json`.
