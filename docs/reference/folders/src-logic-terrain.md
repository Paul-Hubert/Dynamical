# src/logic/terrain

The marching cubes world on the CPU side: chunk constants and components, the density function, CPU marching
cubes (for the colliders and the tests), the ground's curvature, the soil patches and the relief of the ground
textures. Part of `dynamical_sim`; the GPU meshing is in `src/renderer/terrain`.

| File | What |
|---|---|
| [`chunk.h`](../../../src/logic/terrain/chunk.h) | `chunk::` constants (cube 4 m, 64 cubes a side, chunk 256 m, border 1, 67³ samples in `[x][z][y]`, `index`, `samplePosition`); components `ChunkC`, `ChunkVolume`, `ChunkEmpty`; tags `ChunkGenerating`, `ChunkGenerated`, `ChunkMeshPending`, `ChunkDestroying`; `IVec3Hash`; context `ChunkMap` |
| [`terrain_generator.h`](../../../src/logic/terrain/terrain_generator.h) / [`terrain_generator.cpp`](../../../src/logic/terrain/terrain_generator.cpp) | `TerrainGenerator`: FastNoise2 FBm heightfield (5 octaves) + 3D FBm overhangs (3 octaves); `generate` (a chunk, false when no surface), `sample` (any box of the grid, identical values for chunks and colliders), `surfaceHeight(s)` |
| [`marching_cubes.h`](../../../src/logic/terrain/marching_cubes.h) / [`marching_cubes.cpp`](../../../src/logic/terrain/marching_cubes.cpp) | `IndexedMesh`, `MarchingCubes`: the table of `resources/mclookup.bin` (throws when missing), `mesh` with shared vertices (Jolt's internal edges), `vertexNormal` (area weighted, the GPU's `vertexNormal`) |
| [`ground_curve.h`](../../../src/logic/terrain/ground_curve.h) | `groundCurve(density, p, spacing)`: second derivatives (xx, xz, zz) of the ground height from density differences, clamped to `ground_curve_max`; mirrored in `marchingcubes.comp.glsl` (the `curve` of terrain vertices) |
| [`soil.h`](../../../src/logic/terrain/soil.h) / [`soil.cpp`](../../../src/logic/terrain/soil.cpp) | `SoilPatchParams`, `SoilParams` (seed 4242, mud / stones / sand wavelengths and cover), `soil::` map layout (67², RGBA), `grassHeight`, `soilKey`, `SoilField` (distance into each patch, `sample`, `threshold`, `bake` of a chunk's map), `sampleSoilMap` |
| [`ground_relief.h`](../../../src/logic/terrain/ground_relief.h) / [`ground_relief.cpp`](../../../src/logic/terrain/ground_relief.cpp) | `GroundReliefParams`, `groundReliefModel` (a texture's height map as a tile of triangles), `groundReliefKey`, `bakeGroundRelief` (the grass baker run on it: `resources/ground_relief_<name>.bin`) |

## Systems registered here

None. The systems that use these: [ChunkManager](../systems/ChunkManager.md), [ChunkGenerator](../systems/ChunkGenerator.md)
(in `src/logic/systems`), [TerrainColliders](../systems/TerrainColliders.md), [WorldTerrain](../systems/WorldTerrain.md)
(in `src/physics`), and `Terrain` (`TerrainSys`, renderer), `Forest`, `VegetationScatter`.

## Components and context defined here

- Components: [ChunkC](../components/ChunkC.md), [ChunkVolume](../components/ChunkVolume.md), [ChunkEmpty](../components/ChunkEmpty.md)
- Tags: [ChunkGenerating](../components/ChunkGenerating.md), [ChunkGenerated](../components/ChunkGenerated.md), [ChunkMeshPending](../components/ChunkMeshPending.md), [ChunkDestroying](../components/ChunkDestroying.md)
- Context: [ChunkMap](../components/ChunkMap.md), [TerrainGenerator](../components/TerrainGenerator.md) (as `std::shared_ptr<TerrainGenerator>`)

`SoilField` is not a context variable: `SoilMaps` (renderer) and `CameraWalk` own theirs; `SoilParams` is held by
`RenderSettings::soil_params`.

## Main functions and classes

- `TerrainGenerator::sample(start, count, values)`: the one density function of the world; `> 0` is solid.
- `chunk::index(x, y, z)`: the `[x][z][y]` layout the shader expects.
- `MarchingCubes::mesh`, `vertexNormal`; `groundCurve`: CPU mirrors of `marchingcubes.comp.glsl`.
- `SoilField::bake(chunk)`: what `SoilMaps` uploads per chunk.

## Dependencies

FastNoise2, glm, EnTT; [`src/logic/grass`](src-logic-grass.md) (`ground_relief` uses the grass baker); [`src/util`](src-util.md) (`readFile`).

## Design docs

[docs/ground.md](../../ground.md), [docs/ground-relief.md](../../ground-relief.md), [docs/grass/13-see-through.md](../../grass/13-see-through.md) (`curve`),
[docs/port-mc-grass-gltf-vr-jolt.md](../../port-mc-grass-gltf-vr-jolt.md).

## Tests

`TERRAIN.*` ([`tests/unit/terrain_normals.cpp`](../../../tests/unit/terrain_normals.cpp)), `SOIL.*` ([`tests/unit/soil.cpp`](../../../tests/unit/soil.cpp)), `RELIEF.*` ([`tests/unit/ground_relief.cpp`](../../../tests/unit/ground_relief.cpp)).
