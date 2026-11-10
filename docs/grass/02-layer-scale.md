# 2. Per-layer tile size

**Goal.** Two layers of the same period repeat with that period. With tiles of 1 and 1.37 m the pair only repeats after 137 m, and the second layer has wider, sparser blades: two sizes of grass.

## Design

- Layer scales: 1, 1.37, 1.73, 0.81, each blended towards 1 by `layer_spread`.
- The layer loop is rewritten on the layer description of [plan.md](plan.md): per layer, the tile space ray is divided by its scale, `reach` and the mip follow (`lod - log2(scale)`), and the hit is compared by distance along the view ray.
- Heights are not scaled: a wider blade is not taller.

## Settings

`layer_spread` (0 to 1, default 1): 0 gives equal tiles.

## Check

Far camera: the regular grid of tufts of the two-layer picture should be gone. Top camera: two blade widths visible. The CPU mirror is per layer and does not change.

## Result

Done. Scales 1, 1.37, 1.73 for the tall layers (0.55 is the undergrowth of step 4), blended towards 1 by `layer_spread`.

Seen: two blade widths at the top and 45° cameras, the second layer wider and sparser. No artefact from the per-layer `reach` and mip.
