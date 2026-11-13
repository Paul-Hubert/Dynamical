# src/logic/lighting

The part of the irradiance probes that needs no GPU ([lighting](../../lighting.md)): which probes of a chunk are
traced, filled or left alone, the light a probe stores, and where the chunks' bricks are in the atlas. Mirrored by
`shaders/include/probes.glsl` and `probe_update.rt.comp.glsl`; used by the renderer's `ProbeVolume`
(`src/renderer/lighting/`). No systems, no registry access.

## Files

| File | What |
|---|---|
| [`probes.h`](../../../src/logic/lighting/probes.h) | namespace `probes` (a brick of 65³ probes per chunk, one per density sample, 4 m apart; `index`; classes Rock, Fill, Active, Open; `reach` 8 m), `PlantBounds`, `ProbeClasses`, `classifyProbes`, `ProbeLight` (L1 spherical harmonics per colour folded with the cosine: `add`, `irradiance`), `fibonacciDirection`, `ProbeBricks` (pages of 2 x 2 x 2 bricks, a 16³ wrapping table of chunk coordinates) |
| [`probes.cpp`](../../../src/logic/lighting/probes.cpp) | `classifyProbes` (air near the ground or a plant's box is traced, ground next to the air is filled from its neighbours, by box dilation of the masks), `fibonacciDirection`, `ProbeBricks::allocate`, `release`, `find` |

## Systems

None.

## Components and context variables

None defined. The renderer's [ProbeVolume](../systems/service-ProbeVolume.md) reads the chunks' ChunkVolume and the
plants for `classifyProbes`.

## Worth knowing

- A probe stores x = the mean radiance and yzw = twice the mean of radiance times direction, per colour: what a
  white surface of normal n sees is x + dot(yzw, n).
- A brick's texel layout is x first; a chunk and the one 16 chunks further share a cell of the indirection table,
  only one of them can have a brick (`allocate` returns -1 then, as when every page is full). Pages are never given
  back.

## Dependencies

- `src/logic/terrain/chunk.h` (chunk sizes, `IVec3Hash`).
- Used by `src/renderer/lighting/` (`ProbeVolume`) and `tests/unit/lighting.cpp` (`LIGHT.5` to `LIGHT.7`).

## Design docs

- [docs/lighting.md](../../lighting.md), [docs/lighting/built.md](../../lighting/built.md)
