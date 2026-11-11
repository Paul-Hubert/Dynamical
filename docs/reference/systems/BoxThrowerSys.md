# BoxThrowerSys

> **Kind:** system · **Registry name:** `BoxThrower` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/systems/system_list.h`](../../../src/view/systems/system_list.h), [`src/view/systems/base_scene.cpp`](../../../src/view/systems/base_scene.cpp), [`src/view/spawn.cpp`](../../../src/view/spawn.cpp)

## Purpose
Left click throws a 1 m dynamic box from the desktop camera.

## Lifecycle
- **Construction / params:** `DEFINE_SYSTEM(BoxThrowerSys)`, no JSON params.
- **preinit:** None (empty).
- **init:** None (empty).
- **tick(dt):** if `Input::leftClick` and `!ImGui::GetIO().WantCaptureMouse`: `forward = camera.forward()`; increment a function-static `count`; colour `(0.4 + 0.6 * ((count*37) % 100) / 100, same with 61, same with 83, 1)`; `spawnBox(reg, camera.position + forward * 3, 1, colour, forward * 25)`.
- **finish:** None (empty).

## Expected behavior
- One box per click frame (at most one per frame, `leftClick` is a per-frame flag), launched at 25 m/s along the view direction, 3 m in front of the camera.
- No box when clicking over an ImGui window.
- Colours cycle deterministically with the click count; the counter is function-static (shared by instances).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Input`](../components/Input.md) | host | read | tick | `leftClick` |
| [`Camera`](../components/Camera.md) | host | read | tick | `position`, `forward()` |
| [`ModelManager`](../components/ModelManager.md) | host | read | tick | spawnBox |
| [`Transform`](../components/Transform.md) | host | write +create | tick | spawnBox |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | write +create | tick | spawnBox |
| [`RigidBody`](../components/RigidBody.md) | host | write +create | tick | spawnBox, Dynamic box with `initial_velocity` |

## Side effects
- Creates a host entity per throw (Jolt body follows in the physics systems).

## Ordering and dependencies
- After [InputSys](InputSys.md) and [UISys](UISys.md); after camera writers ([PlayerControlSys](PlayerControlSys.md)) to throw from this frame's pose.
- Before the host physics systems to get the body this frame.

## Related
[BoxRingSys](BoxRingSys.md), [DebugMenuSys](DebugMenuSys.md), [VRControlSys](VRControlSys.md), [folder `src/view/systems/`](../folders/src-view-systems.md).
