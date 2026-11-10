# 1. Per-layer noise warp

**Goal.** The layers are the same tile turned and shifted, bent by one shared noise: wherever layer 0 repeats, layer 1 repeats with it. Each layer gets its own bend, so the pair never lines up the same way twice.

## Design

- The noise texture (512², three channels of tiled noise) is read once per layer, at the layer's root position times `noise_frequency`, with a per-layer frequency factor (1, 1.37, 0.73, 1.61) and offset. Two channels give the offset; the pair of channels rotates with the layer (xy, yz, zx).
- The offset is added to the layer's tile coordinate, in tiles, times `noise_amplitude`.
- The position read is the middle of the grass in root space, before the warp (not the shell pixel).

Limit to respect: amplitude × frequency × 2π / (noise feature size) under about 0.3, or blades stretch. With the texture's 10 features across, a frequency of 0.03 per tile gives features of 3.3 tiles: amplitude 0.2 is at the limit, 0.15 is safe.

## Settings

`noise_frequency`, `noise_amplitude` (existing). No new one.

## Check

Far and eye cameras: with a single shared warp, both layers show the same wavy bands; after, bands of one layer cross those of the other. A blade must not slide when the camera moves (low camera, two positions).

## Result

Done. Each layer reads the noise once at its own root position, with its own frequency factor, offset and pair of channels. The default amplitude went from 0.2 to 0.15 tiles.

The layer loop was rewritten for this step on the layer description of the plan (hit compared by distance); with one shared height the picture is the one of before, as expected.

Seen: no stretching of the blades at 0.15. On its own the step changes little in a still: with two layers already turned by the golden angle, no repetition could be pointed at from these cameras before either. Its use is with step 2, and in motion over a large field, which the captures do not show. Blade sliding with the camera was not checked on captures.
