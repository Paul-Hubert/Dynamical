# `tests/golden/`

> [Source](../../../tests/golden/) · Parent: [`tests/`](tests.md)

## Summary
Reference outputs of the `gpu` tests. Replacing one is done with `tools/rebaseline.py <name> --reason ...`, which appends the reason to `CHANGELOG.md`.

## Files
| File | Contents |
|---|---|
| `images/*.png` | capture goldens: `base_spawn`, `base_ring` (base scene), `worlds_boxes`, `worlds_boxes_close` (test_boxes_view) |
| `base_probe.csv` | `Probe` output of `scene=base probe_entities=RigidBody probe_every=60 ticks=900` (S1.1) |
| `CHANGELOG.md` | why each golden was added or replaced (e.g. the Jolt 5.5 to 5.6 friction change re-recording `base_spawn`, `base_ring`, `base_probe.csv`) |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
`images/` (no separate doc).
