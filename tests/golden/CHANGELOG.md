# Golden changes

Every golden added or replaced, with the reason. Newest last.

## Jolt 5.6 drift (P2 step 1, 2026-10-31)

Moving from Jolt 5.5.0 (vcpkg baseline) to 5.6.0 (overlay port `ports/joltphysics`) changes `base`'s ring
boxes; nothing else in the image changes. Evidence:

- **Only Jolt changed.** On Jolt 5.5 the scene-driven `base` matched master bit for bit (commit `b37f099`:
  S1.1 0 m, V1.1 MAE 0). The next build only adds the overlay port.
- **Still deterministic.** With 5.6, two runs give identical captures (V0.1: MAE 0) and probes (S0.1: 0 m).
- **Free fall is unchanged; contact is not.** Probing every frame on 5.6 gives each box's first ground
  contact. At frame 60, the 8 boxes that landed flat between frames 42 and 45 are bit-identical to 5.5;
  the 4 that differ (1, 7, 8, 9: 2e-4 to 2e-2 m) are the ones in contact earlier (frames 2 to 41: sliding,
  or touching the slope at spawn). Differences then grow while boxes tumble down the slopes (5.8 m for box
  10 at frame 180) and stop once every box sleeps (frames 480 to 900 unchanged). The `base_ring` heatmap is
  confined to the boxes; terrain, grass and UI pixels are identical. `base_spawn` moves less (MAE
  0.25/255, within the thresholds) because the boxes are far away.
- **Cause.** Jolt 5.6.0 replaced per-contact-point friction by per-manifold friction: two linear
  constraints and one torsional constraint at the average contact point, bounded by mu * sum(lambda_n)
  (release notes v5.6.0; `ContactConstraintManager.h:306-307`; docs/physical-agents/jolt-readouts.md
  section 0). A box resting on a face sees no tangential impulse in either model, so flat landings stay
  identical; sliding and tumbling boxes receive different friction impulses and, on hilly terrain,
  diverge chaotically.

`base_spawn`, `base_ring` and `base_probe.csv` are re-recorded on Jolt 5.6.0 with this reason.

## Grass from baked 3D rays (2026-11-10)

`GrassPass` no longer looks up 2D rays against circles (`raycast_data.bin`): it looks up rays baked against a
generated grass model, at 45° and from the top (docs/grass.md). Every grass pixel of `base` changes, by design:
blades instead of dots up close, a smooth green far away (the textures have mips), a shell 0.6 m high instead
of 1 m that comes down to the ground at the rock. Boxes, rock, sky and UI are untouched; the probe goldens
still match (S0.1, S1.1), and two runs give identical captures (V0.1: the baker and the wind are deterministic).

The same day, on the same branch, the ten steps of docs/grass/plan.md: layers of several tile sizes and heights,
height patches, an undergrowth layer, blade colours, curved blades, blade shadows, 256 texels per tile. The
terrain pass now darkens the flat ground under the grass and pushes its depth back by the grass height
(docs/grass/03-patches.md), so terrain pixels under the grass change too.

`base_spawn` and `base_ring` are re-recorded once, after the last step, with this reason. `worlds_boxes*` have
no grass pass and still match.

## Sky and lights (lighting L1 to L9, 2026-11-11)

The goldens had not followed master since the grass rays: they show `base` without the soil patches, their
textures, the height map of the grass or the Soil lines of the debug menu, which master 00d9400 has. They are
re-recorded once, on the lighting branch, where every scene also changes by design (docs/lighting.md, docs/lighting/built.md):

- the sky is drawn (a gradient, the disc of the sun) where the clear colour was, and far things fade into it;
- the ambient light is the sky's (bluer, a little brighter on what faces up) and the ground's bounce;
- the debug menu has a Sky section, and with ray tracing a Lighting one.

The goldens are the pictures **without ray tracing** (`tools/visual_test.py` passes `ray_tracing=false` to
`golden` and `repeat`): with it they depend on the GPU. The sun of `base` is stopped in captures, where the
shaders had it before. Probes are untouched (S0.1, S1.1 pass), two runs give identical captures (V0.1).

| Date | Golden | Change | Base commit | Reason |
|---|---|---|---|---|
| 2026-10-31 | `images/base_spawn.png` | added | 9605a57 | master baseline, recorded in P0 before any behaviour change |
| 2026-10-31 | `images/base_ring.png` | added | 9605a57 | master baseline, recorded in P0 before any behaviour change |
| 2026-10-31 | `base_probe.csv` | added | 9605a57 | master baseline: the 12 ring boxes every 60 frames for 900 frames (fixed 1/60 clock) |
| 2026-10-31 | `images/base_spawn.png` | replaced | de8e4fb | Jolt 5.6 friction model (see Jolt 5.6 drift note above) |
| 2026-10-31 | `images/base_ring.png` | replaced | de8e4fb | Jolt 5.6 friction model (see Jolt 5.6 drift note above) |
| 2026-10-31 | `base_probe.csv` | replaced | de8e4fb | Jolt 5.6 friction model (see Jolt 5.6 drift note above) |
| 2026-10-31 | `images/worlds_boxes.png` | added | cf3b065 | new golden (P2 V2.1): 4 box worlds at frame 300 |
| 2026-10-31 | `images/worlds_boxes_close.png` | added | cf3b065 | new golden (P2 V2.1): close-up of world 3, capsule orientation |
| 2026-11-10 | `images/base_spawn.png` | replaced | e580e24 | grass from baked 3D rays (see note above) |
| 2026-11-10 | `images/base_ring.png` | replaced | e580e24 | grass from baked 3D rays (see note above) |
| 2026-11-11 | `images/base_spawn.png` | replaced | 6db72a6 | master's soil, grass and menu changes, then the sky and the lights (see note above) |
| 2026-11-11 | `images/base_ring.png` | replaced | 6db72a6 | master's soil, grass and menu changes, then the sky and the lights (see note above) |
| 2026-11-11 | `images/worlds_boxes.png` | replaced | 6db72a6 | the sky and the lights, the Sky section of the menu (see note above) |
| 2026-11-11 | `images/worlds_boxes_close.png` | replaced | 6db72a6 | the sky and the lights, the Sky section of the menu (see note above) |
