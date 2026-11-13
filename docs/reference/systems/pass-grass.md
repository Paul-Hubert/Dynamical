# grass

`GrassPass` in [`src/renderer/passes/grass_pass.cpp`](../../../src/renderer/passes/grass_pass.cpp) · Scope: Host · Needs renderer: yes
· Lanes: `renderer.passes` (`"grass"`) · Parameters: no

Grass without blade geometry: every terrain chunk is drawn again as a shell at the top of the plants, and
each pixel of it looks its view ray up in rays baked against tiles of plants (`grass.frag.glsl`,
[docs/grass.md](../../grass.md), [docs/grass/plan.md](../../grass/plan.md)). The pass owns the baked rays
of the ten kinds of plants, the noises the species are chosen by, and a height map per chunk (the top of
its plants) that `update()` bakes on the main thread as the chunks get their meshes.

## Behaviour

**Constructor** (main thread, when the renderer is built):

- Sets `RenderSettings::grass_pass = true` (`reg.ctx().get<RenderSettings>()`: the `Renderer` emplaces it
  before the passes): the terrain pass then darkens and pushes the ground under the grass.
- `grass_kind=<name>` (`Settings::grass_kind`): that kind everywhere (`forced_kind` specialization
  constant); an unknown name logs a warning.
- For each of the ten `GrassKind`s: `./resources/grass_raycast_<kind>.bin` is loaded when its key
  (`grassBakeKey`) matches, else baked (`bakeGrass`, logged with its time) and written (a warning when it
  cannot be). Their mips are built (`makeGrassTextures`, `makeGrassColour`, 7 levels at most) and the
  kinds merged level by level into eight 2D array images (BC4 / BC5 / BC1 sRGB where the GPU filters all
  three, else R8 / RG8 / RGBA8 sRGB): oblique height, top height, oblique normal, oblique id, top normal,
  top id, oblique colour, top colour. An error is logged when the layers exceed `maxImageArrayLayers`.
  One info line gives the size and the time. `generateGrassTile` gives each kind's modelled tile size and
  height; `grassKindInfo` its abundance and whether it is small.
- The species noise (`bakeSpeciesNoise`, RGBA8 snorm, repeating) and a 512² tileable noise (`TiledNoise`,
  three octaved channels) are uploaded.
- The height map array: `min(TerrainSys::max_slots, maxImageArrayLayers)` layers of `grass_height::size`²
  R8, cleared to 255; a `ChunkLayers` hands out its layers.
- **Set 1** (eleven combined image samplers): 0 oblique heights, 1 top heights, 2 tile noise, 3 height
  maps (vertex stage), 4 oblique normals, 5 oblique ids, 6 top normals, 7 top ids, 8 oblique colours, 9 top
  colours, 10 species noise. The baked rays use a trilinear sampler with
  `RenderSettings::textures.grass_anisotropy` (16 without settings); the noises a repeating linear one; the
  height maps a clamped linear one.
- Pipeline layout: set 0 (`Context::view_layout`), set 1, one push constant range `GrassPC` (vertex +
  fragment).

**createPipeline(target, renderpass)**: `grass.vert.glsl.spv` + `sceneFragmentShader(ctx, "grass")`, the
terrain vertex input, 38 specialization constants (azimuths per elevation, `low_from`, `low_to`, tile
size, height and abundance of each kind, `small_mask`, `forced_kind`). With a stencil in the scene depth
format (`Context::sceneStencil()`) three pipelines per target: `pipelines` (front faces where stencil = 0),
`cuts` (front faces where stencil = 1, seen through) and `backs` (`grass_back.frag.glsl.spv`, front faces
culled, no colour or depth write, stencil replaced with 1). Without a stencil, one pipeline.

**update(frame_index)** (main thread, every frame, before any recording):

1. Returns without `RenderSettings` or `TerrainBuffers`.
2. Builds the `GrassTopParams` from `grass_params.species`, `patch_amount`, `height_map`, the forced kind
   and the heights of the kinds; a new `GrassTopField` when its key changed.
3. `ChunkLayers::update` over the chunks with `ChunkGPU` + `ChunkReady`: frees layers retired more than
   `NUM_FRAMES + 1` frames ago; gives a layer to every chunk without a map (its slot taken by another
   entity counts as none) and to at most 16 chunks a frame whose map was baked with another key; for each
   it bakes the map from `ChunkC::pos` (`GrassTopField::bake`) and queues an image update into that layer
   (`Transfer::updateImage`). Slots whose chunk is gone have their layer retired.

**record(command, view)** (both targets concurrently; reads only):

1. Returns without `TerrainBuffers`, without settings, when `RenderSettings::grass` is false, or before the
   first `update()` (no `topField`).
2. Fills `GrassPC` from `RenderSettings::grass_params` (base normal and `follow_terrain`, tile size,
   height, patch size, the top of the height map, noise, wind, `top_start`, `layers` with
   `species.overlap` in thousandths over the low byte, layer spread and patch, `patch_amount`, colour
   variation, dryness, curve, shadow, canopy, silhouette, azimuth dither, baked colour, species size and
   shade), binds sets 0 and 1.
3. Draws the shell of every chunk with `ChunkC` + `ChunkGPU` + `ChunkReady` that has a height map layer:
   pushes `chunk` (origin x, z, height map layer, soil map layer from `SoilLayers` or -1), binds
   `TerrainBuffers::vertices[slot]`, one indirect draw.
4. With `grass_params.silhouette > 0` and the stencil pipelines: the back faces first (mark), then the
   front faces whole (silhouette 0), then the cut ones (silhouette = `grass_params.silhouette`). Else the
   front faces once.

**Destructor**: pipelines (three arrays), layout, views, samplers, set layout, pool.

## Parameters

None from the scene. What it reads of the settings:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `RenderSettings::grass` | bool | true | false: nothing drawn |
| `RenderSettings::grass_params` | struct | see `render_settings.h` | everything the shaders take: the push constants; `species`, `patch_amount`, `height_map` also key the height maps |
| `RenderSettings::textures.grass_anisotropy` | int | 16 | anisotropy of the baked rays (constructor) |
| `Settings::grass_kind` (`grass_kind=`) | string | empty | one kind everywhere |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| RenderSettings | context | readwrite | constructor, update, record | sets `grass_pass = true` (constructor); reads anisotropy, `grass`, `grass_params` |
| Settings | context | read | constructor | `grass_kind` |
| TerrainBuffers | context | read | update, record | number of slots, vertex buffers, indirect draws |
| SoilLayers | context | read | record | soil map layer of each chunk (where no grass grows) |
| ChunkC | component | read | update (bake callback), record | chunk position for the height map, origin for the draw |
| ChunkGPU | component | read | update (`ChunkLayers::update`), record | slot of the chunk's mesh |
| ChunkReady | tag | read | update (`ChunkLayers::update`), record | only drawable chunks get a map and a draw |

## Side effects

- Reads or bakes and writes `./resources/grass_raycast_<kind>.bin` for the ten kinds (the bake takes seconds per kind); uploads about 250 MB of ray textures (less in blocks).
- Uploads the species noise, the tile noise and a 2048 layer (or device limit) height map array.
- Per frame: queues image updates of the height maps of new chunks, and of up to 16 chunks after a change of `grass_params.species`, `patch_amount` or `height_map` (`Transfer::updateImage`).
- Logs: one info line per kind baked, the total size of the rays; a warning for an unknown `grass_kind` or an unwritable file; an error when the GPU has too few array layers.
- Records up to three draws of every chunk's shell per target (stencil marked by the back faces).

## Order and dependencies

- Needs `TerrainSys` (meshes, `TerrainBuffers`, `ChunkGPU`, `ChunkReady`) and `SoilMaps` (`SoilLayers`, set 0 binding 2).
- Drawn after the terrain pass: the shell is depth tested against the ground, and the stencil marks the back faces in front of it. The terrain pass reads the `grass_pass` flag this constructor sets.
- `update()` runs after the systems of the frame (inside `Renderer::render`), so a chunk that got its mesh this frame gets its height map before it is recorded.

## Used in scenes
<!-- generated -->

## Tests

- `GRASS.*` in [`tests/unit/grass.cpp`](../../../tests/unit/grass.cpp) (the bake, the lookup the shader mirrors, the height map `GrassPass::update` bakes: GRASS.8) and [`tests/unit/grass_textures.cpp`](../../../tests/unit/grass_textures.cpp) (the textures the pass uploads: GRASS.9 "the pass gets the heights apart from the normals and the ids", the blocks, the colour map, the species, the shell at the top of the plants GRASS.15).
- Visual: `V0.1`, `V0.2`, `V1.1`, `V2.2` (scene `base`), `P1 check` (`base_interactive.capture.json` toggles the grass); `tests/capture/grass.capture.json`, `grass_times.capture.json` (by hand).
