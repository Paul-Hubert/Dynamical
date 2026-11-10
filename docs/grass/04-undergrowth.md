# 4. Undergrowth layer

**Goal.** Between the tall tufts the pixel falls to a flat dark ground. A short, dense layer fills it: depth between the blades, and no ground from above.

## Design

- One more layer after the tall ones: height `undergrowth` × shell height, tile scale 0.55 (blades about half as wide, four times as many per m²), its own turn, shift and warp.
- It is a normal layer of [plan.md](plan.md): entered `(H - Hi) / D` under the shell, closest hit wins. From low angles the tall layers hide it; from above it shows between them.
- Its colour is the same grass, a little darker (it is under the canopy; step 8 does this properly).

## Settings

`undergrowth` (0 to 1, default 0.45): 0 removes the layer.

## Check

Top and 45° cameras: no flat dark ground between the tufts. Eye camera: unchanged skyline, boxes hidden a little more at their base.

## Result

Done. Layer of scale 0.55 and height `undergrowth` × shell (0.45), with half the layer patches of the tall ones.

Seen: from the top and at 45° the flat dark ground between the tufts is replaced by fine short grass. From eye level the tall layers hide it, as expected.
