# src/renderer/terrain

The terrain on the GPU: the marching cubes of the chunks (the `Terrain` system: compute shader recorded into the
frame's compute commands, a vertex buffer and an indirect draw per chunk slot, the meshes never come back to the
CPU but for their triangle counts with ray tracing), the soil maps of the chunks
([SoilMaps](../systems/service-SoilMaps.md)) and the allocation of the layers of per-chunk maps
([ChunkLayers](../systems/service-ChunkLayers.md)).

## Files

| File | What |
|---|---|
| [`terrain_sys.h`](../../../src/renderer/terrain/terrain_sys.h) / [`terrain_sys.cpp`](../../../src/renderer/terrain/terrain_sys.cpp) | `TerrainVertex` (32 bytes: position, octahedral normal, curve), the components `ChunkGPU`, `ChunkReady`, `ChunkTriangles`, the context variable `TerrainBuffers`, and `TerrainSys` (registered as `Terrain` from `src/view/register_view.cpp`): up to 8 chunks meshed a frame, nearest first, 2048 slots of 2^15 triangles, slots recycled after `NUM_FRAMES + 1` frames, destroys `ChunkDestroying` chunks |
| [`chunk_layers.h`](../../../src/renderer/terrain/chunk_layers.h) | `ChunkLayers`: which layer of a 2D array holds the map of each chunk slot, given when the chunk gets its mesh, retired `NUM_FRAMES + 1` frames after; re-bakes on a key change, a budget a frame |
| [`soil_maps.h`](../../../src/renderer/terrain/soil_maps.h) / [`soil_maps.cpp`](../../../src/renderer/terrain/soil_maps.cpp) | the `SoilLayers` context variable and `SoilMaps`: a 67² RGBA8 soil map per chunk (`SoilField::bake`) in a 2D array image, binding 2 of the view set |

## Systems

- [Terrain](../systems/Terrain.md) (`TerrainSys`, registered in `src/view/register_view.cpp`, host post tick).

Objects that touch the registry: [SoilMaps](../systems/service-SoilMaps.md), [ChunkLayers](../systems/service-ChunkLayers.md).

## Components and context variables

- [ChunkGPU](../components/ChunkGPU.md) (component): the mesh slot of a chunk.
- [ChunkReady](../components/ChunkReady.md) (tag): the mesh is drawable.
- [ChunkTriangles](../components/ChunkTriangles.md) (component): triangle count read back from the GPU (ray tracing).
- [TerrainBuffers](../components/TerrainBuffers.md) (context): the indirect buffer and the vertex buffers per slot.
- [SoilLayers](../components/SoilLayers.md) (context): the soil map layer of each chunk.

## Worth knowing

- Readers of `ChunkGPU` + `ChunkReady` + `TerrainBuffers`: the `terrain` and `grass` passes (indirect draw per
  slot), `RayScene` (a structure per chunk, from `ChunkTriangles`), `ChunkLayers`.
- `TerrainSys::max_slots` (2048) also bounds the layers of the soil and grass height maps.

## Dependencies

- `src/logic/terrain` (`ChunkC`, `ChunkVolume`, `ChunkMeshPending`, `ChunkDestroying`, `ChunkGenerating`,
  `SoilField`, `soil::` sizes), `src/logic/components` (`Camera`, `RenderSettings`), `src/renderer/context`,
  `src/renderer/util/pipeline.h`; the shader `marchingcubes.comp.glsl`.

## Design docs

[docs/ground.md](../../ground.md) (soil maps), [docs/lighting.md](../../lighting.md) (triangle counts for the rays),
[docs/grass/11-height-map.md](../../grass/11-height-map.md) (the other user of `ChunkLayers`),
[docs/port-mc-grass-gltf-vr-jolt.md](../../port-mc-grass-gltf-vr-jolt.md) (GPU marching cubes).
