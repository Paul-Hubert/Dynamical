# src/logic/grass

The grass without geometry ([grass](../../grass.md)): the tiles of blades and plants the rays are baked against,
the baking itself (oblique rays at four elevations and rays from the top, RGBA8 hit height, octahedral normal,
blade id, colour), its compression to the textures the grass pass uploads, the CPU mirror of the shader's lookup,
and the world fields that say how tall the grass is and which plants grow where. Part of `dynamical_sim`; no
systems, no registry access (the renderer's `GrassPass`, `tools/raycast` and the ground relief use it).

## Files

| File | What |
|---|---|
| [`grass_model.h`](../../../src/logic/grass/grass_model.h), [`grass_model.cpp`](../../../src/logic/grass/grass_model.cpp) | `GrassKind` (meadow and nine plants), `GrassPart`, `GrassKindInfo`, `grassKindInfo`, `grassKindFromName`, `GrassModel` (triangles, normals, blade ids, parts, colours, tile size, height), `GrassModelParams`, `generateGrassTile` (ribbons, tubes, spindles, leaflets), `grassTileSites` (best-candidate order), `writeGrassObj` |
| [`grass_bake.h`](../../../src/logic/grass/grass_bake.h), [`grass_bake.cpp`](../../../src/logic/grass/grass_bake.cpp) | `GrassTracer` (rays against the tile repeated to infinity, a grid of cells), `grassElevation`, `grassReach`, `GrassBakeParams`, `GrassRaycast` (the baked images), `grassBakeKey`, `bakeGrass`, `bakeGrassModel`, `saveGrassRaycast` / `loadGrassRaycast` (`resources/grass_raycast_<kind>.bin`), `rgbaMipChain`, normal and colour encodings, `sampleGrassOblique` / `Top` (+ colour), `GrassLookup`, `lookupGrass` (the shader on the CPU), `grassSeeThrough` |
| [`grass_textures.h`](../../../src/logic/grass/grass_textures.h), [`grass_textures.cpp`](../../../src/logic/grass/grass_textures.cpp) | BC4 and BC1 encoders and decoders, `GrassTexture`, `GrassTextures` (height R8/BC4, normal RG8/BC5, id R8/BC4 per level), `makeGrassTextures`, `makeGrassColour`, `compressedGrassRaycast` |
| [`grass_height.h`](../../../src/logic/grass/grass_height.h), [`grass_height.cpp`](../../../src/logic/grass/grass_height.cpp) | `GrassHeightParams`, namespace `grass_height` (65² map per chunk on the terrain vertices), `grassHeightKey`, `GrassHeightField` (octaves of FastNoise2 Perlin noise anchored to the world; `sample`, `bake`), `sampleGrassHeightMap` |
| [`grass_species.h`](../../../src/logic/grass/grass_species.h), [`grass_species.cpp`](../../../src/logic/grass/grass_species.cpp) | `GrassSpeciesParams`, namespace `grass_species` (the repeating noise texture of how much and how tall of each kind), `bakeSpeciesNoise`, `GrassSpecies`, `grassSpeciesAt` (the four kinds of a place, their densities, fades and heights, as `species()` in `grass.frag.glsl`), `GrassTopParams`, `grassTopKey`, `GrassTopField` (the top of the plants per chunk, the shell's height map) |

## Systems

None.

## Components and context variables

None. Its parameters live in [RenderSettings](../components/RenderSettings.md) (`logic/components/render_settings.h`
includes `GrassHeightParams` and `GrassSpeciesParams`).

## Worth knowing

- The baked files are made by `dynamical` in the run directory when missing or stale: every bake has a key
  (`grassBakeKey`, `grassHeightKey`, `grassTopKey`) that changes with its parameters and with the baker.
- An oblique ray is stored where it crosses the middle of the grass and looked up there too, in the two elevations
  around the view ray's, blended along their reach ([12-elevations](../../grass/12-elevations.md)).
- Everything the shaders do has a CPU mirror here (`lookupGrass`, `grassSpeciesAt`, `grassSeeThrough`,
  `sampleGrassHeightMap`) so the tests and `raycast --preview` can check it.
- Plant ids are ranks in a best-candidate order of the tile: any first part of the plants is spread evenly, which
  is how species densities drop plants (`hitPlant`).

## Dependencies

- `src/logic/terrain/chunk.h` (chunk sizes), FastNoise2; baking runs on its own std::thread workers (`threads`, default all cores).
- Used by `src/renderer/passes/grass_pass.cpp`, `src/logic/terrain/ground_relief.*` (the relief's grass rays),
  `src/logic/components/render_settings.h`, `src/logic/sandbox/life.cpp` (`GrassHeightField` for the sandbox's
  grass), `tools/raycast`, `tests/unit/grass.cpp`, `tests/unit/grass_textures.cpp`.

## Design docs

- [docs/grass.md](../../grass.md), [docs/grass/plan.md](../../grass/plan.md) and one doc per feature in
  [docs/grass/](../../grass/plan.md): [11-height-map](../../grass/11-height-map.md),
  [12-elevations](../../grass/12-elevations.md), [13-plants](../../grass/13-plants.md),
  [13-see-through](../../grass/13-see-through.md), [14-colour-map](../../grass/14-colour-map.md),
  [15-species](../../grass/15-species.md)
