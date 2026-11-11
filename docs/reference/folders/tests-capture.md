# `tests/capture/`

> [Source](../../../tests/capture/) · Parent: [`tests/`](tests.md)

## Summary
Capture scripts for dynamical (`capture=<file>`, read by `view/capture.*`): a `scene`, optional scripted `input` events by frame (keys, mouse buttons and positions) and `shots` (`frame`, `name`, optional `camera` with `pos` / `look_at` or `offset`). Run by `tools/visual_test.py` from the `gpu` CTest entries.

## Files
| File | Contents |
|---|---|
| `base.capture.json` | `base`: `base_spawn` at frame 600, `base_ring` at 900 from a set camera (V0.1, V0.2, V1.1, V2.2) |
| `base_moved.capture.json` | `base_spawn` at frame 600 with a camera `offset` of 0.1 m: imgdiff must fail (P0 check) |
| `base_interactive.capture.json` | scripted W key, clicks: camera flight, box throw, grass toggle (P1 check) |
| `worlds_boxes.capture.json` | `test_boxes_view`: `worlds_boxes` at frame 300 and `worlds_boxes_close` at 301 (V2.1) |
| `worlds_stepper.capture.json` | `test_boxes_view` with scripted clicks pausing and single-stepping `WorldStepper` (P2 check) |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
