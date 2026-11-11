# PlayerControlSys

> **Kind:** system · **Registry name:** `PlayerControl` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/systems/system_list.h`](../../../src/view/systems/system_list.h), [`src/view/systems/player_control.cpp`](../../../src/view/systems/player_control.cpp)

## Purpose
The free-flying desktop camera. Emplaces the [`Camera`](../components/Camera.md) context variable, places it at a spawn point above the terrain, turns it with the mouse while the right button is held and moves it with WASD / Space / Left Ctrl (Shift sprints, wheel scales the speed). The R action (`Input::VR_RIG`) moves the VR play area under the camera.

## Lifecycle
- **Construction / params:** `DEFINE_SYSTEM(PlayerControlSys)`, no JSON params.
- **preinit:** `reg.ctx().emplace<Camera>()` (defaults: position (0, 150, 0), yaw 0, pitch -0.4, fov 70 deg).
- **init:** `camera.position = (0, 0, 60)`, `pitch = -0.3`; if `std::shared_ptr<TerrainGenerator>` exists, `position.y = surfaceHeight(0, 60) + 20`.
- **tick(dt):**
  1. `looking = input.rightDown`; `SDL_SetRelativeMouseMode(looking)` (called every tick).
  2. While looking: `yaw -= mouseDelta.x * 0.0025`, `pitch = clamp(pitch - mouseDelta.y * 0.0025, -1.55, 1.55)`.
  3. Early-out if `ImGui::GetIO().WantCaptureKeyboard` (no movement, no VR_RIG handling).
  4. If `mouseWheel.y != 0` and ImGui does not want the mouse: function-static `base_speed` (initial 30) *= 1.25 (up) or 0.8 (down), clamped to [1, 2000].
  5. `speed = base_speed * (SPRINT ? 8 : 1) * dt`; move vector from FORWARD/BACKWARD (`camera.forward()`, pitch included), RIGHT/LEFT (`camera.right()`), UP/DOWN (world Y); `camera.sprinting = on[SPRINT]`; if non-zero, `position += normalize(move) * speed`.
  6. If `on[VR_RIG]`: clear it; `VRRig.position = camera.position` with `y = surfaceHeight(x, z)` when the generator exists; `VRRig.yaw = camera.yaw`.
- **finish:** None (empty).

## Expected behavior
- Mouse look still works while ImGui has keyboard focus; movement does not.
- Diagonal movement is normalized (same speed as axis movement).
- `base_speed` is function-static: shared by all instances and kept across scene reloads within a process.
- Other systems may overwrite the camera later in the frame ([FixedCameraSys](FixedCameraSys.md) at init, [SphereViewSys](SphereViewSys.md), [RollerPlayerSys](RollerPlayerSys.md) DesktopRoller, [Capture](Capture.md) before the systems tick).
- VR_RIG requires a `VRRig` context variable (`get`, emplaced by the renderer's VR render target).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Camera`](../components/Camera.md) | host | write +create | preinit | `emplace<Camera>()` |
| [`Camera`](../components/Camera.md) | host | write | init | spawn position and pitch |
| [`TerrainGenerator`](../components/TerrainGenerator.md) (`std::shared_ptr<TerrainGenerator>`) | host | read | init | `find`, `surfaceHeight(0, 60)` |
| [`Camera`](../components/Camera.md) | host | readwrite | tick | `yaw`, `pitch`, `position`, `sprinting` |
| [`Input`](../components/Input.md) | host | readwrite | tick | reads `rightDown`, `mouseDelta`, `mouseWheel`, movement bits; clears `on[VR_RIG]` |
| [`VRRig`](../components/VRRig.md) | host | write | tick | `position`, `yaw` on VR_RIG |
| [`TerrainGenerator`](../components/TerrainGenerator.md) (`std::shared_ptr<TerrainGenerator>`) | host | read | tick | `surfaceHeight` under the camera on VR_RIG |

## Side effects
- `SDL_SetRelativeMouseMode` every tick (grabs/hides the cursor while the right button is held).
- Reads ImGui IO (`WantCaptureKeyboard`, `WantCaptureMouse`).

## Ordering and dependencies
- After [InputSys](InputSys.md) and [UISys](UISys.md) (`rightDown` is masked by UISys).
- preinit must precede any system that `get<Camera>()` in init ([FixedCameraSys](FixedCameraSys.md), renderer); it emplaces `Camera` for the whole host.
- The generator lookup in init only succeeds if ChunkGeneratorSys (or the physics terrain colliders) emplaced it in preinit.
- Before [RollerPlayerSys](RollerPlayerSys.md) DesktopRoller (which reads the yaw it set) and [DebugMenuSys](DebugMenuSys.md) (whose button sets VR_RIG: handled next frame if DebugMenu runs later).

## Related
[`Camera`](../components/Camera.md), [VRControlSys](VRControlSys.md), [FixedCameraSys](FixedCameraSys.md), [folder `src/view/systems/`](../folders/src-view-systems.md).
