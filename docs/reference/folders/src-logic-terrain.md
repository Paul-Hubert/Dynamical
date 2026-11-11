# `src/logic/terrain/`

> [Source](../../../src/logic/terrain/) · Parent: [`src/logic/`](src-logic.md)

## Summary
Data model and CPU algorithms of the marching cubes world. A chunk is 64^3 cubes of 4 world units (256 units per side), its density sampled on a 67^3 grid (one-sample border for gradient normals) laid out `[x][z][y]`, > 0 solid. Chunk entities live in the host registry, streamed by ChunkManagerSys and filled by ChunkGeneratorSys; their lifecycle tags (`ChunkGenerating`, `ChunkGenerated`, `ChunkMeshPending`, `ChunkDestroying`) are consumed by TerrainSys (GPU meshing, destruction) and TerrainColliders (physics). `TerrainGenerator` is the thread-safe FastNoise2 density function shared through a `std::shared_ptr<TerrainGenerator>` context variable (host: ChunkGeneratorSys, worlds: WorldTerrainSys). `MarchingCubes` is the CPU mesher with shared vertices that TerrainColliders uses for Jolt mesh shapes; it is not stored in a registry.

## Files
| File | Contents |
|---|---|
| [`chunk.h`](../../../src/logic/terrain/chunk.h) | `chunk::` constants (`cube_size` 4, `cubes` 64, `length` 256, `border` 1, `values` 67, `num_values`), `chunk::index` ([x][z][y]), `chunk::samplePosition`; `ChunkC` (pos, `origin()`, `center()`), `ChunkVolume`, `ChunkEmpty`, the four lifecycle tags, `IVec3Hash`, `ChunkMap` (`get(pos)`). |
| [`terrain_generator.h`](../../../src/logic/terrain/terrain_generator.h) | `TerrainGenerator` interface and parameters (seed 1337, base_level 60, height_amplitude 90, height_frequency 1/700, overhang_amplitude 14, overhang_frequency 1/90). |
| [`terrain_generator.cpp`](../../../src/logic/terrain/terrain_generator.cpp) | Noise graph (Simplex FBm 5 octaves gain 0.5 lacunarity 2 for height; 3-octave FBm for overhangs, seed + 1); `sample(start, count, values)`: `base_level + height_amplitude * h(x, z) - y + overhang_amplitude * n(x, y, z)` at sample indices; `generate(pos, values, mean)`: samples from `pos * 64 - 1`, returns whether both signs occur (`> 0` and `<= 0`), sets the mean; `surfaceHeight(x, z)` (heightfield part only). |
| [`marching_cubes.h`](../../../src/logic/terrain/marching_cubes.h) | `IndexedMesh` (vertices, CCW-from-outside indices), `MarchingCubes` (loads `./resources/mclookup.bin`, throws if missing or wrong size; `mesh(values, samples, cube_size, out)`). |
| [`marching_cubes.cpp`](../../../src/logic/terrain/marching_cubes.cpp) | Corner / edge tables matching `marchingcubes.comp.glsl`; one vertex per crossed grid edge (linear interpolation), degenerate triangles dropped, winding checked once per mesh against the density gradient and flipped if needed. |

## Systems defined here
None (the chunk systems are in [`src/logic/systems/`](src-logic-systems.md)).

## Components defined here
- [ChunkC](../components/ChunkC.md)
- [ChunkVolume](../components/ChunkVolume.md)
- [ChunkEmpty](../components/ChunkEmpty.md)
- [ChunkGenerating](../components/ChunkGenerating.md)
- [ChunkGenerated](../components/ChunkGenerated.md)
- [ChunkMeshPending](../components/ChunkMeshPending.md)
- [ChunkDestroying](../components/ChunkDestroying.md)
- [ChunkMap](../components/ChunkMap.md)
- [std::shared_ptr&lt;TerrainGenerator&gt;](../components/std--shared_ptr-TerrainGenerator.md)

## Subfolders
None.
