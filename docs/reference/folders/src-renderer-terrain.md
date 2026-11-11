# `src/renderer/terrain/`

> [Source](../../../src/renderer/terrain/) · Parent: [`src/renderer/`](src-renderer.md)

## Summary
GPU meshing of the voxel terrain. `TerrainSys` (registered as `Terrain`, host scope, post lane, needs renderer) records the marching cubes compute shader for chunks carrying `ChunkMeshPending`, manages per-chunk mesh slots (vertex buffer + indirect draw command) with deferred recycling, and destroys `ChunkDestroying` chunk entities. Results are published as the `ChunkGPU` / `ChunkReady` components and the `TerrainBuffers` context variable, read by `TerrainPass` and `GrassPass`. Chunk data components (`ChunkC`, `ChunkVolume`, the state tags) are defined in `src/logic/terrain/chunk.h`.

## Files
| File | Contents |
|---|---|
| `terrain_sys.h` | `TerrainVertex` (pos, normal, uv; 32 bytes), `ChunkGPU { uint32_t slot; }`, `ChunkReady` tag, `TerrainBuffers` context variable, `TerrainSys` (limits `max_per_frame = 8`, `max_slots = 2048`, `max_triangles = 32768`) |
| `terrain_sys.cpp` | `MCPushConstants`, `TerrainSys::init` (buffers, `mclookup.bin`, descriptors, compute pipeline), `allocateSlot`, `tick`, destructor |

## Systems defined here
- [TerrainSys](../systems/TerrainSys.md)

## Components defined here
- [ChunkGPU](../components/ChunkGPU.md)
- [ChunkReady](../components/ChunkReady.md)
- [TerrainBuffers](../components/TerrainBuffers.md) (context)

## Subfolders
None.
