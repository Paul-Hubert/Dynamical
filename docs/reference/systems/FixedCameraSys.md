# FixedCameraSys

> **Kind:** system · **Registry name:** `FixedCamera` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/fixed_camera.h`](../../../src/view/fixed_camera.h), [`src/view/fixed_camera.cpp`](../../../src/view/fixed_camera.cpp)

## Purpose
Places the desktop [`Camera`](../components/Camera.md) once at init at a JSON-given position looking at a JSON-given point (used by `test_boxes_view`). It does nothing per frame, so [PlayerControlSys](PlayerControlSys.md) can still fly the camera afterwards.

## Lifecycle
- **Construction / params:** `FixedCameraSys(reg, params)`:
  - `pos`: array of 3 floats, default `[0, 10, 20]`.
  - `look_at`: array of 3 floats, default `[0, 0, 0]`.
  - Read with `params[key][i].get<float>()` when the key is present; no other validation (a non-array or short array throws a nlohmann exception). Unknown keys are ignored.
- **preinit:** None (default).
- **init:** `get<Camera>()`; `position = pos`; `dir = normalize(look_at - pos)`; `pitch = asin(clamp(dir.y, -1, 1))`; `yaw = atan2(-dir.x, -dir.z)`.
- **tick(dt):** None (empty).
- **finish:** None (default).

## Expected behavior
- After init, `camera.forward()` points from `pos` toward `look_at`.
- `pos == look_at` gives a NaN direction (not checked).
- A [Capture](Capture.md) shot without a camera spec keeps this pose as the spawn camera (Capture records the camera on its first frame).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Camera`](../components/Camera.md) | host | write | init | `position`, `pitch`, `yaw` |

## Side effects
None.

## Ordering and dependencies
- Must come after [PlayerControlSys](PlayerControlSys.md) in the scene list: PlayerControl emplaces `Camera` in preinit and writes its own spawn pose in init; FixedCamera's init must run later to win.

## Related
[PlayerControlSys](PlayerControlSys.md), [Capture](Capture.md), [folder `src/view/`](../folders/src-view.md).
