# 10. Sharper blades (idea 14)

**Goal.** Close up, blades are soft and thin ones are doubled: a blade is 3 texels wide at the root, and two azimuths 5.6° apart are blended, which shows the blade twice at the top and bottom of the grass.

## Design

Two independent changes.

- **Texture size.** `GrassBakeParams::size` 128 to 256: 4 times the rays (bake 0.3 to about 1 s in RelWithDebInfo), 19 MB of textures with mips instead of 5. The file key changes, the next run bakes again.
- **One azimuth per pixel.** Instead of blending the two nearest azimuth slices, each pixel takes one, chosen with the blend weight as probability from a fixed screen pattern (interleaved gradient noise). The average over neighbouring pixels is the same blend, but each pixel shows one sharp view. The grain is at the pixel scale, in a picture that is noise at that scale already. Far away (mip over 1) the blend comes back: slices are averaged there anyway.

## Settings

`azimuth_dither` (0 to 1, default 0.5): 0 is the blend of before.
Texture size is a bake parameter (`GrassBakeParams`), not a live setting.

## Check

Low camera, blades at 1 to 2 m: thin blades single and sharp. `raycast --preview` error at 45° and 90° for 256 against 128 (0.10 and 0.03 against 0.11 and 0.06 were measured before the plan). Turning the camera must not make the pattern jump (two captures 2° apart).

## Result

Done: 256 is the default, the azimuth dither is at half.

| | 45° | 90° |
|---|---|---|
| 128 × 128 × 64 | 0.111 | 0.060 |
| 256 × 256 × 64 | 0.103 | 0.034 |

(`raycast --preview`, mean error of the hit height.) Bake time 0.27 s to 0.99 s in RelWithDebInfo, not measured in Debug.

- **Texture size.** Blades at 1 to 2 m are visibly finer. Mips are means of blades: with twice the texels the same distance reads one mip further and thin blades faded sooner, so the mip is biased by -0.8 (`mip_bias` in the shader): blades stay visible in the middle distance, slightly aliased.
- **One azimuth per pixel.** At 1 the pixel grain is plain to see on a still crop, more than the doubling it removes. `azimuth_dither` defaults to 0.5: the blend weight is pushed half way to the nearer slice. It fades out between mip 0.5 and 1.5. Two captures of the same view are identical (the pattern is fixed on the screen); turning the camera was not captured.
