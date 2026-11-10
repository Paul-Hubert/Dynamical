# 6. Curved blades (idea 8)

**Goal.** Wind leans the blades as a straight shear: the whole blade tilts like a stick. A lean that grows with height bends it: the root holds, the tip goes.

## Design

A blade point at height `y` (0 root, 1 top of the layer) is displaced by

```
d(y) = Hi (lean y + bend y²)
```

`lean` is the straight part (base normal, terrain normal, the part `1 - curve` of the wind), `bend` the curved part (the part `curve` of the wind). The tip moves as far as before, the middle of the blade less. `d(0) = 0`: roots stay put.

The view ray is a parabola in root space. The lookup needs a straight ray, so:

1. **Tangent at the middle.** The ray is straightened around `y = 0.5`: slope `lean + bend`, offset `d(0.5)`. First lookup as today, which gives the height `h` of the hit.
2. **Move the ray for that depth.** At `h`, the straight ray is off by `-Hi bend (h - 0.5)²` (zero at the middle, largest at the top and at the ground: again the reason to work from the middle). The lookup is done again at the position moved by that much.

The normal is tilted by the slope of the blade at the hit, `lean + 2 bend h`.

One more oblique fetch per layer, only while `bend` is not zero.

## Settings

`curve` (0 to 1, default 0.7): part of the wind that bends instead of leaning. `wind_strength` is unchanged.

## Check

45° camera, two frames one second apart: tips move, the base of the tufts does not. `curve = 0` gives the picture of step 5.

## Result

Done as designed: tangent at the middle, then one more lookup moved by `-Hi bend (h - 0.5)²`. The lookup is a function (`lookup`) since this step.

Seen: no artefact on the two frames one second apart. The effect itself is small in a still with the default wind (the tip moves 0.12 × the height, 4 to 7 cm) and was not checked in motion: no video was made.
