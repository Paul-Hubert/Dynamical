# `src/view/components/`

> [Source](../../../src/view/components/) · Parent: [`src/view/`](src-view.md)

## Summary
Data types of the dynamical front end that live in the host registry: the desktop input state (`Input`, a context variable filled by InputSys), the renderable instance component (`ModelRenderable`, drawn by the renderer's ModelPass), and the legacy calendar clock (`Time`, with its `Date` helper). Engine-wide components shared with the headless library (Transform, Camera, VRRig, RigidBody, ...) live in `src/logic/components/` instead.

## Files
| File | Contents |
|---|---|
| `input.h` | `Input` (host context var): `Action` enum (FORWARD ... VR_RIG), action bitset, mouse position / wheel / delta, button held / down / click flags, text input, focus and window state. |
| `model_renderable.h` | `ModelRenderable` (host component): `model` entity holding a ModelC, RGBA `tint`. |
| `time.h` | `Time` (host context var, game seconds, speed and pause modifier) and `Date` (calendar breakdown, not stored). |

## Systems defined here
None.

## Components defined here
- [Input](../components/Input.md)
- [ModelRenderable](../components/ModelRenderable.md)
- [Time](../components/Time.md)

## Subfolders
None.
