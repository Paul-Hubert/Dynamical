# terrain

`TerrainPass` in [`src/renderer/passes/terrain_pass.cpp`](../../../src/renderer/passes/terrain_pass.cpp) · Scope: Host · Needs renderer: yes
· Lanes: `renderer.passes` (`"terrain"`) · Parameters: no

Draws the marching cubes chunks: the earth and the patches of the soil maps (mud, stones, sand) seen from
above, the rock on the steep faces, each layer coming through the others by the height of its texture, the
relief of the mud, the stones and the sand looked up in baked rays, and the ground darkened under the grass
(`terrain.frag.glsl`, [docs/ground.md](../../ground.md), [docs/ground-relief.md](../../ground-relief.md)).
It has no `update()`: everything it draws is already on the GPU (the vertex buffers and indirect draws of
`TerrainSys`, the soil maps of `SoilMaps`); `record()` only reads the registry.

## Behaviour

**Constructor** (main thread, when the renderer is built, once per scene that lists `"terrain"`):

- Descriptor pool and the layout of **set 1** (five combined image samplers, fragment stage): binding 0 the
  old `resources/textures/grass.png` + `rock.jpg` array (sampler linear, repeat), 1 the ground albedo array,
  2 the ground detail array (normal + height + occlusion), 3 the oblique relief rays, 4 the relief rays from
  the top. Set 0 is `Context::view_layout` (ViewUBO, tree shade, soil maps, lights, and with ray tracing the
  rays and the probes).
- `loadGround()`: reads `resources/ground/ground.json` (through `libraryResourceFile("ground")`: the build
  folder, else the source tree), five layers `mud, stones, sand, rock, earth`, each with `tile` (metres;
  becomes tiles per metre), `smoothness`, `color`, `detail`. The ten images are decoded with their mips
  (`decodeTextureMips`) as tasks of the `tf::Executor` context variable (a local executor when there is
  none), then uploaded as two 2D arrays (`R8G8B8A8Srgb`, `R8G8B8A8Unorm`). Any failure (missing manifest,
  missing layer, bad file): a warning, and 1x1 flat colours (the mean colours of the five kinds) instead;
  `groundLibrary` false.
- `loadRelief()`: with the textures, for mud, stones and sand, the height channel (blue) of the detail
  texture is baked into rays (`bakeGroundRelief`) or read back from `./resources/ground_relief_<name>.bin`
  when its key (`groundReliefKey`) matches; a stale or missing file is baked again (logged with its time)
  and written. The heights of the hits go to two 2D arrays (oblique: a layer per azimuth of each elevation,
  the three soils one after the other; top: one layer each), BC4 where the GPU filters it, else R8. Without
  the textures: a flat texel, relief off (`reliefBaked` false).
- The ground sampler is anisotropic: `RenderSettings::textures.ground_anisotropy` (8 without settings),
  clamped to the device limit.
- Pipeline layout: sets 0 and 1, one fragment push constant range (`TerrainPC`).

**createPipeline(target, renderpass)** (once per target: desktop, and the headset when VR is on):
`terrain.vert.glsl.spv` + `sceneFragmentShader(ctx, "terrain")` (`terrain.frag.glsl.spv`, or the
`.rt.spv` variant with ray tracing), `Context::sceneAttachments()` colour attachments (1, or 2 with ray
tracing: colour and sun of `SunVisibility`), the terrain vertex input (`terrainVertexBindings()` /
`terrainVertexAttributes()`: position, octahedral normal, curve; shared with the grass pass).
Specialization constants 0 to 3: azimuths of each elevation of the relief rays, 4 and 5: `low_from`,
`low_to` of the lowest rays.

**record(command, view)** (both targets concurrently, once per eye in VR; reads only):

1. Returns without `TerrainBuffers`, or when `RenderSettings::terrain` is false.
2. Binds the pipeline of the target and sets 0 (`view.view_set`) and 1.
3. Pushes `TerrainPC` once: tiles per metre and smoothness of the five layers, `under_grass` =
   `grass_params.height` when `RenderSettings::grass` and `grass_pass` (set by `GrassPass`) are both true,
   else 0; `library`; `scree` (`RenderSettings::scree`, 1 without settings); the depth of the relief of
   each soil (0.05, 0.12, 0.04 m) times `RenderSettings::relief` times `Settings::ground_relief`, 0 when the
   relief is not baked.
4. For every chunk with `ChunkC`, `ChunkGPU` and `ChunkReady`: pushes `chunk` (origin x, z; the layer of its
   soil map from `SoilLayers::layer(slot, entity)`, -1 without soil maps; texels per metre
   `1 / chunk::cube_size`), binds `TerrainBuffers::vertices[slot]` and draws `TerrainBuffers::indirect` at
   `slot * indirect_stride` (one indirect draw a chunk).

**Destructor**: destroys pipelines, layouts, samplers, views, pool (images are shared pointers).

## Parameters

None from the scene. What it reads of the settings:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `RenderSettings::terrain` | bool | true | false: nothing drawn |
| `RenderSettings::textures.ground_anisotropy` | int | 8 | anisotropy of the ground sampler (read at construction) |
| `RenderSettings::grass`, `grass_pass`, `grass_params.height` | bool, bool, float | true, false, — | how far the ground is darkened and pushed under the grass |
| `RenderSettings::scree` | float | 1 | stones at the foot of the rock, 0 none |
| `RenderSettings::relief` | float | 1 | scale of the relief depths, 0 flat |
| `Settings::ground_relief` (`ground_relief=` on the command line) | float | 1 | the same, from the run settings |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| RenderSettings | context | read | constructor, record | ground anisotropy; `terrain`, `grass`, `grass_pass`, `grass_params.height`, `scree`, `relief` |
| Settings | context | read | record | `ground_relief` scale |
| Executor | context | read | loadGround | `tf::Executor` that decodes the ground textures (local executor when absent) |
| TerrainBuffers | context | read | record | per slot vertex buffers and the indirect draw buffer |
| SoilLayers | context | read | record | layer of the soil map of each chunk |
| ChunkC | component | read | record | chunk origin |
| ChunkGPU | component | read | record | slot of the chunk's mesh |
| ChunkReady | tag | read | record | only chunks whose mesh is drawable |

## Side effects

- Reads `resources/textures/grass.png`, `resources/textures/rock.jpg`, `resources/ground/ground.json` and its textures; uploads them as GPU texture arrays (transfer queue).
- Reads or bakes and writes `./resources/ground_relief_{mud,stones,sand}.bin` in the run directory; uploads the relief rays (BC4 or R8 arrays).
- Decodes the ground textures as taskflow tasks on the shared executor (waits for them in the constructor).
- Logs a warning without ground textures, an info line per relief baked, a warning when a relief file cannot be written.
- Creates the terrain pipeline of each target; records one indirect draw per ready chunk into the target's command buffer.

## Order and dependencies

- Needs `TerrainSys` (it creates `TerrainBuffers`, `ChunkGPU`, `ChunkReady`); without it nothing is drawn.
- `SoilMaps` (renderer, always there) provides `SoilLayers` and binding 2 of set 0; its `update()` runs
  before the passes' updates every frame, so a chunk has its soil layer when recorded.
- Reads `RenderSettings::grass_pass`, which the `GrassPass` constructor sets: the order of the passes in
  the scene does not matter, the passes are all built before the first frame.
- Drawn after the sky (`SkyPass` is always first) and before the grass in the usual scene order
  (`"terrain", "grass", ...`): the grass shell is depth tested against the terrain.

## Used in scenes
<!-- generated -->

## Tests

- No unit test drives the pass (it needs a GPU). The visual tests `V0.1`, `V0.2`, `V1.1`, `V2.2`
  (`tests/capture/base.capture.json`, scene `base`) capture it.
- The CPU sides it uploads: `RELIEF.*` in [`tests/unit/ground_relief.cpp`](../../../tests/unit/ground_relief.cpp) (the relief bake), `SOIL.*` in [`tests/unit/soil.cpp`](../../../tests/unit/soil.cpp) (the soil maps it reads), `TERRAIN.*` in [`tests/unit/terrain_normals.cpp`](../../../tests/unit/terrain_normals.cpp) (the vertex normals it decodes).
