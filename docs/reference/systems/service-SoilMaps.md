# SoilMaps

`SoilMaps` in [`src/renderer/terrain/soil_maps.cpp`](../../../src/renderer/terrain/soil_maps.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by [Renderer](service-Renderer.md), always there) · Parameters: no (`RenderSettings::soil`, `soil_params`)

The patches of mud, stones and sand of the ground (`SoilField`, [`src/logic/terrain/soil.h`](../../../src/logic/terrain/soil.h))
on the GPU: a 67 x 67 RGBA8 map per chunk (the terrain vertices plus a border of one; RGB how far into a patch of
each kind, A the height of the grass next to the patches) in one layer of a 2D array image, baked when the chunk
gets its mesh and again when the parameters change. Binding 2 of the view set: `terrain.frag.glsl` and the grass
shaders read it. The passes find a chunk's layer in the `SoilLayers` context variable and push it per draw.

## Behaviour

- **Constructor**: one `R8G8B8A8Unorm` 2D array image of `min(TerrainSys::max_slots = 2048, maxImageArrayLayers)`
  layers, uploaded blank (no soil anywhere until a chunk has its map) through the transfer queue; its 2D-array
  view and a linear, clamp-to-edge sampler go in `Context::soil_view` / `soil_sampler`; emplaces `SoilLayers`
  with a [ChunkLayers](service-ChunkLayers.md) of that many layers.
- **update()** (`Renderer::render`, main thread, before the transfer submission): nothing until both
  `RenderSettings` and `TerrainBuffers` (made by the `Terrain` system's `init`) exist. Then:
  - `SoilLayers::enabled = RenderSettings::soil` (false: `layer()` answers -1, the passes draw no soil);
  - the key of `RenderSettings::soil_params` (`soilKey`); a new `SoilField` when it changed;
  - `ChunkLayers::update` over the chunk slots (`TerrainBuffers::vertices.size()`), with at most 16 re-bakes a
    frame of maps baked with another key (new chunks are not counted); each bake reads the chunk's `ChunkC::pos`,
    bakes its map on the CPU (`SoilField::bake` at `(pos.x, pos.z)`) and uploads it into its layer
    (`Transfer::updateImage`, stays in shader-read layout).
- **Destructor**: erases `SoilLayers`, waits for the device, destroys the view and the sampler.

## Parameters

None of its own: `RenderSettings::soil` (default true) and `RenderSettings::soil_params` (`SoilParams`: the
kinds, their cover, noise and edge width; docs/ground.md).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| SoilLayers | context | readwrite+create+destroy | constructor (emplace), update, destructor (erase) | the layer of each chunk's map, `enabled` |
| RenderSettings | context | read | update | `soil`, `soil_params` |
| TerrainBuffers | context | read | update | number of chunk slots (`vertices.size()`); its absence means no terrain yet |
| ChunkC | component | read | update (bake callback) | chunk coordinates of the map to bake |
| ChunkGPU | component | read | update (`ChunkLayers::update`) | the chunk's mesh slot |
| ChunkReady | tag | read | update (`ChunkLayers::update`) | only chunks with a drawable mesh get a map |

## Side effects

- GPU: one 2D array image (67 x 67 x 4 bytes x up to 2048 layers, about 37 MB), a layer upload per chunk that gets
  its mesh or a stale map; sets `Context::soil_view` / `soil_sampler`.
- CPU: the bake of a map on the main thread (`SOIL.2` measures how long it takes).

## Order and dependencies

- After the `Terrain` system's tick of the frame (it gives `ChunkGPU` + `ChunkReady` in the post tick) and before
  the passes' `update` and recording, which read `SoilLayers` (`TerrainPass`, `GrassPass`).
- Layers freed `NUM_FRAMES + 1` frames after their chunk is gone or re-baked (no frame in flight reads them).

## Used in scenes
<!-- generated -->

## Tests

- `SOIL.1` to `SOIL.3` ([`tests/unit/soil.cpp`](../../../tests/unit/soil.cpp)): the maps `SoilField::bake` gives,
  their cover and key. The upload and the layers are in the visual tests of the terrain scenes.
