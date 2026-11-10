# 8. Canopy occlusion (idea 12)

**Goal.** The grass darkens with the height of the hit alone: a blade alone in a clearing is as dark at its base as one deep in a tuft. Sky light should depend on what is above.

## Design

- The top texture is the height of the canopy over every point of the tile. One fetch at the hit point (the winning layer's tile space, as in step 7) gives `canopy`.
- `under = max(canopy - h, 0)`: how much grass is over the hit, in layer heights. Occlusion = `exp(-k under)`, mixed with the present height term, which stays as the part that cannot be known from one column (the neighbours).
- A hit on the side of a blade has the rest of that blade above it: the base of a blade is darker than its tip without any neighbour, which is right.
- The undergrowth layer is under the tall ones: its occlusion also uses its own depth under the shell.

## Settings

`canopy` (0 to 1, default 0.6): 0 keeps the height term only.

## Check

Top and 45° cameras: tufts get a dark heart, blades in the open stay light down to the ground.

## Result

Done as designed: `occlusion = mix(height term, mix(0.3, 1, exp(-3 under)) × mix(0.6, 1, baked), canopy)`. The undergrowth gets a further × 0.75 to 1 with its height under the shell.

Seen: blades in the open are lighter down to the ground and the field is brighter overall than at step 7; hearts of tufts stay dark.
