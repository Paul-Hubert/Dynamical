# `tests/onnx/`

> [Source](../../../tests/onnx/) · Parent: [`tests/`](tests.md)

## Summary
Policy inference tests, compiled only with `DYNAMICAL_ONNX` (ONNX Runtime): `PolicyRunner` against Python reference outputs, `ModelWatcher` checkpoint selection, the `Policy` system in worlds, and the `vr_roller` scene's players.

## Files
| File | Contents |
|---|---|
| `policy_runner.cpp` | SR.13 matches Python onnxruntime on 100 observations (`tests/data/onnx/`); SR.14 refuses a wrong layout or missing file; SR.15 `ModelWatcher` latest-step and most-recent-run selection; SR.16 (scenario) the trained policy beats the controller on marching cubes |
| `vr_roller.cpp` | SR.18: `vr_roller` with the VR player on the small sphere and the desktop player on the wheel, `VRRoller` and `DesktopRoller` in order |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
