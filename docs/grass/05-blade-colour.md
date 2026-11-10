# 5. Per-blade colour

**Goal.** Every blade has the same green, only its height in the grass changes it. A random value per blade, baked, lets world noise decide which blades are dry, dark or light: colour that does not repeat with the tile.

## Design

**Bake.** The texel becomes: r height, g b the normal in octahedral coordinates (pole on +y: the upper hemisphere is the inner diamond, no seam is ever crossed), a the blade id, a random value in 0..1 per blade (0.5 on the ground). `GrassModel` gets one id per triangle. The baker version changes: cached files are baked again.

Ids are filtered like the rest: on the edge of a blade the id slides to the neighbour's, and far away it tends to 0.5, the neutral colour.

**Shader.**
- Brightness: `1 + colour_variation × (id - 0.5)`.
- Dry blades: a blade is dry when `id < dryness × n`, with `n` in 0..1.5 from the noise at the root (patches of dry grass, a few dry blades everywhere else). Dry is a straw colour, lighter at the tip.
- Hue: a slight shift to yellow or blue green with a second hash of the id (`fract(id × 7.3)`).

## Settings

`colour_variation` (0 to 1, default 0.5), `dryness` (0 to 1, default 0.25).

## Check

`GRASS.3` decodes the octahedral normal and checks it against the traced one; a new case checks that the two triangles hit on one blade give one id and that ids differ between blades. Cameras: individual blades can be told apart at eye level; dry patches at the far camera.

## Result

Done. Baker version 2: texel = height, octahedral normal, blade id. `GrassModel::ids` holds one id per vertex (blades share no vertex).

- `GRASS.3` checks height, normal and id of the texel and of `lookupGrass` against the traced hit; `GRASS.7` checks the normal encoding (within 0.8° as computed, 2.6° on 8 bits), one id per blade, 0.5 on the ground.
- Dry blades: `id < dryness × 1.5 × n`, straw colour. Over mip 1 to 3 the test becomes the same part of the colour, so a dry patch does not turn all straw with distance.

Seen: single blades can be told apart at eye level; dry blades scattered everywhere, more in patches a few metres wide; the far camera shows the patches as a slight change of hue.
