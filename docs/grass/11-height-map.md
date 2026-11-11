# 11. A height map per chunk

**Goal.** The field patches of [step 3](03-patches.md) came from the 512 texel noise texture, one feature every `patch_size` (14 m) and the same field again every 140 m. The height of the grass is now a property of the world: octaves of Perlin noise at a low frequency, baked once per chunk in a 2D map.

## Design

- **The noise** (`GrassHeightField`, `src/logic/grass/grass_height.{h,cpp}`, in `dynamical_sim`): FastNoise2 Perlin under a fractal sum, the first octave 96 m long, 4 octaves, each twice finer and half as strong (96, 48, 24 and 12 m). The value is spread around the middle (`contrast`) and clamped to 0..1: 0 the shortest grass, 1 the tallest. It is anchored to the world and does not repeat.
- **The map.** 65 × 65 texels of 8 bits for a chunk, one on each column of terrain vertices (4 m): texel (i, j) is the noise i, j cubes from the origin of the chunk. The last row and column are the first ones of the next chunks, baked from the same world samples, so the shell has no step between chunks. Octaves under two texels per wavelength (8 m) cannot be in the map and are left out (`grass_height::octaves`).
- **On the GPU.** One layer of a 2D array (R8, 2048 layers, 8.7 MB) per chunk slot of `TerrainSys`. `GrassPass::update` bakes the map of a chunk on the main thread when the chunk gets its mesh, before it is drawn, and uploads it through the transfer queue (`Transfer::updateImage`). Layers have their own free list: a map that is replaced goes to a new layer and the old one is let go `NUM_FRAMES + 1` frames later, like the chunk slots.
- **In the shader.** `grass.vert.glsl` reads the map of its chunk (origin and layer in the push constants, one push per draw) at the vertex, linear, clamped: `shell = height × slope × (1 - patch_amount × (1 - map))`. Still per vertex and not per pixel: the shell stays at the top of the grass. The layer patches of step 3 are unchanged (the noise texture, per pixel, `patch_size`).
- **Changing the parameters** (debug menu) changes their key: the maps are baked again, 16 chunks per frame, each chunk keeping its old map until then.

## Settings

`RenderSettings::Grass::height_map` (`GrassHeightParams`): `wavelength` (m, 96), `octaves` (4), `lacunarity` (2), `gain` (0.5), `contrast` (1.6), `seed`. `patch_amount` (0.45) is still how much shorter the shortest grass is, and `height` (1 m) the tallest: the grass goes from 0.55 m to 1 m.

## Check

`GRASS.8` (`tests/unit/grass.cpp`): the map is the field on the vertices, neighbours agree on their shared edge, the CPU mirror of the shader read (`sampleGrassHeightMap`), the spread of the heights, the octave limit, the key.

## Result

- Over 16 chunks with the defaults: mean 0.49, 19 % of the texels under 0.2 and 18 % over 0.8, at most 0.26 between two vertices (12 cm of grass over 4 m).
- A map bakes in 18 µs (RelWithDebInfo), at most 8 chunks get a mesh per frame.
- Captures of `tests/capture/grass.capture.json` and views across the chunk borders at (0, 0): no step in the shell, the grass around the boxes is taller than before (they stood in a short patch of step 3).
- `base_spawn` and `base_ring` move (MAE 0.89 and 1.49 of 255 at 0.6 m of grass; `base_ring` has 0.58 % of its pixels over the threshold, the limit is 0.5 %).

Limits: the map is read per vertex, a wavelength under about 12 m aliases on the 4 m triangles as before. Chunks one above the other bake the same map twice. The terrain pass still steps back by the tallest grass everywhere.
