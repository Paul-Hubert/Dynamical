# PlayerControl

`PlayerControlSys` in [`src/view/systems/player_control.cpp`](../../../src/view/systems/player_control.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre (in every desktop scene, `common/desktop.json`) · Parameters: no

Owns the desktop [Camera](../components/Camera.md): makes it, puts it at the spawn, and flies it freely (the debug
camera). Scenes that drive the camera otherwise (FixedCamera, CameraWalk, SandboxWatch, the view systems) come after it
and overwrite what it did; a physical player (an entity with [DesktopView](../components/DesktopView.md)) takes the
camera and the mouse from it entirely.

## Behaviour

- **preinit**: emplaces `Camera` (the renderer's views and many systems need it).
- **init**: camera at (0, y, 60), pitch -0.3 rad; y = ground height + 20 m when a `TerrainGenerator` exists, else 0.
- **tick**:
  1. Returns at once when any entity has `DesktopView`.
  2. Right mouse button held (`Input::rightDown`, so not over ImGui): relative mouse mode on, yaw -= dx * 0.0025,
     pitch -= dy * 0.0025 clamped to ±1.55 rad; released: relative mode off.
  3. Returns when ImGui wants the keyboard.
  4. Wheel (not over ImGui): fly speed x1.25 / x0.8 per notch, kept in 1..2000 m/s (30 at start; a function static).
  5. W/S along `forward()`, D/A along `right()`, Space/Ctrl up/down; Shift x8. The move is normalised, times
     speed x dt. `camera.sprinting` = Shift.
  6. `Input::VR_RIG` (R, or DebugMenu's button): clears it, puts the [VRRig](../components/VRRig.md) under the camera
     on the ground (`surfaceHeight`) and gives it the camera's yaw.
- **finish**: nothing.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Camera](../components/Camera.md) | context | readwrite+create | preinit (emplace), init, tick | made in preinit; init puts it at (0, ground + 20, 60) looking down 0.3 rad; tick turns yaw/pitch and moves position, sets sprinting |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init, tick | surfaceHeight() for the spawn height and the play area's floor; std::shared_ptr<TerrainGenerator>, optional |
| [Input](../components/Input.md) | context | readwrite | tick | rightDown, mouseDelta, mouseWheel, the movement actions; clears Input::VR_RIG once used |
| [DesktopView](../components/DesktopView.md) | tag | read | tick | any entity with it (a physical player) owns the camera: the system does nothing |
| [VRRig](../components/VRRig.md) | context | write | tick | on VR_RIG: position under the camera on the ground, yaw = camera yaw |

## Side effects

- SDL_SetRelativeMouseMode on while the right mouse button is held, off otherwise (not touched while a DesktopView player exists).
- The fly speed (30 m/s, x1.25 / x0.8 a wheel notch, 1 to 2000) is a function-static shared by every instance.

## Order and dependencies

- After Input and UI. `VRRig` must exist when R is pressed (the renderer's VR target emplaces it).
- The `TerrainGenerator` is made by ChunkGenerator in its preinit, so the spawn height is right whatever the order of
  the two in the lane.
- Systems that place the camera (FixedCamera, CameraWalk, SandboxWatch, SphereView, WalkerView, GaitView,
  AnimalAgentView, DesktopRoller) must come after it; DesktopRoller relies on its mouse look.

## Used in scenes

<!-- generated -->

## Tests

- `T2.4 base system list ...` (`tests/unit/base_scene.cpp`, stub), `SR.18 vr_roller: ...` (`tests/onnx/vr_roller.cpp`:
  PlayerControl before DesktopRoller).
- GPU: `P1 check: scripted input flies the camera, throws a box and toggles the grass`, base captures.
