# 3. Height and density patches

**Goal.** A field of one height reads as a carpet. Patches of taller and shorter grass, down to clearings, are what breaks the tiling between 5 and 30 m.

> The field patches now come from a height map baked per chunk instead of the noise texture, and `patch_size` only sizes the layer patches: [11-height-map.md](11-height-map.md).

## Design

Two scales, both by changing heights (a blade cannot be removed from the texture: what is behind it is not baked).

- **Field patches, in the vertex shader.** The shell height is multiplied by `1 - patch_amount × n`, with `n` in 0..1 from the noise texture read at the vertex position, one feature every `patch_size` metres. The shell stays tight on the grass, so crests and depth stay right. Terrain vertices are 4 m apart: `patch_size` under about 12 m would alias.
- **Layer patches, per pixel.** Each layer's height is multiplied by `1 - layer_patch × n_i`, with its own noise at the layer's root position, a few metres per feature. Where a layer goes under 5 % of the shell it is skipped: only the other layers show there.

A layer lower than the shell is entered under it: `enter = (H - Hi) / D` ([plan.md](plan.md)).

## Settings

`patch_size` (m, default 14), `patch_amount` (0 to 0.9, default 0.45), `layer_patch` (0 to 1, default 0.35).

## Check

Far and eye cameras: height changes visible on the skyline of the field and around the boxes (how deep they sit). No grass floating over nothing at the edges of patches.

## Result

Done, with one addition that the plan did not have.

- Field patches in the vertex shader and layer patches per pixel as designed. Layer patches cost one more noise fetch per layer (the plan said none): they have to be read at one place for all layers, before the layer's own root is known.
- Layer patches are read at the root of the ray at the middle of the shell, not of the layer: close to the same from every view. A blade can still change height by a few percent with the view angle.

**The terrain under the grass.** Layers lower than the shell are entered further along the ray, where the ground is no longer the plane under the pixel: on creases the hit ended up under the real terrain, which won the depth test and showed as lines. Two changes in the terrain pass, only where the grass pass draws and the grass setting is on (`RenderSettings::grass_pass`, push constant `under_grass`):

- the terrain is darker under the grass (× 0.2 on flat ground, same slopes as the shell): it is in its shade;
- its depth is pushed back along the view ray by the grass height, so the hits of the grass pass win. Models are drawn after: something buried less than the grass height under flat ground would show through it.

Seen: height changes on the skyline of the field and in how deep the boxes sit (the capture spot is in a short patch: 0.33 m of grass instead of 0.6). The lines on the creases are gone, as are most of the thin rock lines of the first version.
