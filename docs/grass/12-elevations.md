# 12. More elevations, anisotropic filtering

**Goal.** The oblique rays were baked at 45° only. Between 45° and the vertical two views were blended and blades looked stretched; under about 20° hits gathered around the middle of the grass, no better in depth than a flat surface ([grass.md](../grass.md), before this step). And the grass is seen at grazing angles most of the time, where one mip for both axes of the pixel is either blurred or aliased.

## Design

**Four elevations**, each with its own number of azimuths (`GrassBakeParams::angles`, lowest first, 96 + 64 + 64 + 32):

| Elevation | Ray | Reach (tiles crossed) |
|---|---|---|
| 0, the lowest rays | from the top of the grass to its middle over 3 tiles: 9.5° under the horizon | 3 (`grass_max_reach`) |
| 1 | 22.5°, top to ground | 2.41 |
| 2 | 45°, top to ground | 1 |
| 3 | 67.5°, top to ground | 0.41 |

All are stored the same way: the texel is the ray that is above it in the middle of its way, r is the part of its way the ray had left where it hit (which is the height of the hit when the ray goes from the top to the ground).

**Why the lowest rays are not horizontal.** A horizontal ray at the top of the grass meets nothing. Horizontal rays at a fixed height in the grass were tried (`--low 0.65,0.65`): every pixel along the view sees the same ray, so blades become streaks without tips, and the hit is 3 to 4 times further from the traced one than with rays that come down (0.50 against 0.17 at 5°, 0.33 against 0.14 at 10°). A view ray along the ground is looked up as if it went down the grass in 3 tiles anyway (`grass_max_reach`); the lowest rays start where it starts and cross the same 3 tiles, at the lowest slope that still meets most blades. `low_from` and `low_to` are the heights at both ends (1 and 0.5): the same height twice gives horizontal rays.

**Lookup.** The view ray is between two baked elevations. Each says where its ray met a blade (`ahead = (0.5 - r) × reach` tiles after the middle) and the view ray takes the height it has over that place, as before. The two heights, normals and ids are blended, the last elevation and the top down rays over 67.5°. Everything under the lowest rays uses them alone.

The blend weight is linear in the reach of the ray (its cotangent), not in its angle: a view carried to a lower ray stays close to it much longer than carried to a higher one (measured below).

`baked`, which colours the blade and darkens it deep in the grass, is the height where the baked ray met the blade: r, but for the lowest rays (`low_to + r × (low_from - low_to)`). The silhouette on the crests keeps r itself: how soon the ray met something.

**Anisotropic filtering.** GPUs do not filter 3D images anisotropically (measured on the RTX 5080: a sampler at 16 on a 3D image changes the picture 7 times less than on a 2D one, and adds no detail). The oblique rays are now a 2D array, one layer per azimuth of each elevation, with mips per layer; the sampler is trilinear with 16 times anisotropy (the top rays use it too), and the shader blends the two nearest azimuth layers itself. Lookups go through `textureGrad` with the footprint of the pixel on the ground: as wide as the view is far, longer along the view by 1 / cos(incidence). The mip then comes from the short axis, and the samples spread along the long one.

**One view per pixel.** `azimuth_dither` now pushes the pixel towards one elevation as well as one azimuth, with a second dither value. The first lookup of a curved blade (step 6), which only finds a depth, always takes one view: one fetch.

## Settings

Bake parameters (`GrassBakeParams`, a change bakes again): `angles` per elevation, `low_from`, `low_to`. `raycast --angles 96,64,64,32 --low 1,0.5`.
`top_start` is now the part of the way from 67.5° to the vertical where the top rays start to blend in.

## Check

`raycast --preview`, `GRASS.3` (the lookup is the traced hit at each baked elevation), `GRASS.9` (closer to the traced rays than from 45° alone at 6°, 12°, 64° and 70°), captures of `tests/capture/grass.capture.json` with and without anisotropy.

## Result

Mean error of the hit height, in grass heights, 40 000 rays from every side (256 texels). Under 18.4° the hit is where the blade is met on the ground, as a height on the ray that is looked up.

| Elevation | 5° | 10° | 15° | 22.5° | 30° | 45° | 60° | 67.5° | 75° | 90° |
|---|---|---|---|---|---|---|---|---|---|---|
| 45° alone (before) | 0.42 | 0.27 | 0.13 | 0.11 | 0.12 | 0.10 | 0.16 | | 0.17 | 0.03 |
| Four elevations | 0.17 | 0.14 | 0.14 | 0.11 | 0.11 | 0.10 | 0.13 | 0.10 | 0.13 | 0.03 |
| Constant height (the mean) | 0.05 | 0.15 | 0.13 | 0.12 | 0.13 | 0.16 | 0.17 | 0.18 | 0.17 | 0.17 |

- Under 10° and over 45° the hits are closer to the traced ones. Between 15° and 45° nothing changed: the 45° rays carried down were already at the floor of the textures.
- At 5° a constant is still closer: nearly every traced ray goes further than the 3 tiles a lookup can tell.
- Blend weight in the reach against the angle: 0.137 against 0.160 at 15°, 0.120 against 0.146 at 18°, the same over 45°.
- Anisotropy: captures at eye level and far have 11 to 14 % more detail (mean difference between neighbouring pixels of the grass), the views from the top are unchanged.
- Bake: 3.2 s in RelWithDebInfo instead of 1 s (the tracer now skips the cells whose blades end under the ray, without which low rays took 4 times longer). The file is 67 MB, the textures 90 MB with their mips, instead of 19.
- A lookup is 1 to 4 anisotropic fetches instead of 1 trilinear one, plus one for the first depth of curved blades. The frame time was not measured, and VR was not tried.

Not done: more than 256 layers need a GPU with more array layers than Vulkan guarantees (most have 2048); the sun shadows still use the 45° rays alone.
