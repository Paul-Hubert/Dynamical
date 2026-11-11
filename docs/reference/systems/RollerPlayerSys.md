# RollerPlayerSys

> **Kind:** system · **Registry name:** `VRRoller`, `DesktopRoller` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/roller_player.h`](../../../src/view/roller_player.h), [`src/view/roller_player.cpp`](../../../src/view/roller_player.cpp)

## Purpose
Host system of the `vr_roller` scene: a player whose locomotion is one world's trained, steered sphere. Each tick it turns player input into a velocity command passed to `steerSphere` (which sets the agent's goal heading and speed), then puts the player on the sphere: the floor is the sphere's lowest point, smoothed in height. Registered twice with a different `Player`:
- `VRRoller` (`Player::VR`): command from the left stick relative to the head direction, or, with the stick at rest and after A was pressed once, from the lean of the head; moves the `VRRig`.
- `DesktopRoller` (`Player::Desktop`): command from WASD relative to the camera yaw; moves the desktop `Camera` to `eye_height` above the floor.

## Lifecycle
- **Construction / params:** `RollerPlayerSys(reg, params, player)`, keys read with `params.value` (wrong types throw, unknown keys ignored):
  - `world` (int, 0): world index; out of range makes tick a no-op (not clamped).
  - `deadzone` (float, 0.15): stick deadzone (VR).
  - `stick_speed` (float, 3.0): m/s at full stick deflection; DesktopRoller uses 2/3 of it, all of it with Shift.
  - `lean_speed` (float, 3.0): m/s at `lean_range` and beyond.
  - `lean_range` (float, 0.08): m.
  - `lean_deadzone` (float, 0.01): m.
  - `head_top` (array of 3 floats, [0, 0.11, 0.09]): top of the head in OpenXR view space; only read if an array of size 3.
  - `floor_smoothing` (float, 10.0): 1/s.
  - `eye_height` (float, 1.6): m (DesktopRoller).
  - Validation: unless `0 < lean_deadzone < lean_range`, logs an error and uses 0.01 and 0.08.
- **preinit:** None (default).
- **init:** VRRoller only: if a `VRRig` exists, `stick_locomotion = false` (VRControl stops moving the rig with the stick).
- **tick(dt):**
  1. Return if no `Worlds` or `world` out of range; return if that world has no entity with `SphereAgent`, `Transform`, `RigidBody` (first one is used).
  2. `steerSphere(agent, command)` with:
     - **vrCommand:** zero unless `VRInput` and `VRRig` exist, the session is running and focused and the head is valid. A press of A (`right.primary_pressed`) stores `lean_origin = head.position + head.rotation * head_top` (stage space) and enables leaning. If the left stick deflection (capped at 1) exceeds `deadzone`: forward = rig rotation * head -Z flattened (zero command if degenerate), `direction = forward * stick.y + right * stick.x`, command = `normalize(direction) * stick_speed * (deflection - deadzone) / (1 - deadzone)`. Else, if leaning: horizontal offset of the current head top from `lean_origin`, speed = `leanSpeed(distance, lean_deadzone, lean_range, lean_speed)`, command = rig rotation * direction * speed when the speed is positive. Else zero.
     - **desktopCommand:** zero if `Input` or `Camera` is missing or ImGui wants the keyboard; forward = `(-sin yaw, 0, -cos yaw)`, right = `camera.right()`; sum of FORWARD/BACKWARD/RIGHT/LEFT; command = normalized * `stick_speed * (SPRINT ? 1 : 2/3)`.
  3. Position = world `Transform` position, replaced by `PhysicsPose` lerped with `WorldStepperState::alpha` (1 when absent). `bottom = position.y - half_extent.x`.
  4. Floor: snap to `bottom` on the first tick or when it differs by more than 2 m (respawn); otherwise `floor += (bottom - floor) * (1 - exp(-floor_smoothing * dt))`.
  5. VRRoller: `VRRig.position = (x, floor, z)` (yaw untouched). DesktopRoller: `Camera.position = (x, floor + eye_height, z)` (yaw/pitch untouched).
- **finish:** None (default).

## Expected behavior
- `steerSphere` with a zero command sets `goal_speed = 0` and keeps the last goal heading.
- The stick overrides the lean while deflected; the lean is unavailable until A is pressed once; each press re-centres it.
- No VR input (headset off, system menu, no headset) gives a zero command: the sphere is asked to stop.
- The play area / camera follows the interpolated sphere, so it matches where [WorldViewSys](WorldViewSys.md) draws it.
- Desktop movement ignores the camera pitch.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`VRRig`](../components/VRRig.md) | host | write | init | VRRoller: `stick_locomotion = false` |
| [`Worlds`](../components/Worlds.md) | host | read | tick | world lookup |
| [`SphereAgent`](../components/SphereAgent.md) | world | write | tick | `steerSphere`: `goal_heading`, `goal_speed` |
| [`Transform`](../components/Transform.md) | world | read | tick | sphere position fallback |
| [`PhysicsPose`](../components/PhysicsPose.md) | world | read | tick | interpolated position |
| [`RigidBody`](../components/RigidBody.md) | world | read | tick | `half_extent.x` |
| [`WorldStepperState`](../components/WorldStepperState.md) | host | read | tick | `alpha` |
| [`VRInput`](../components/VRInput.md) | host | read | tick | VRRoller: session, head, A press, left stick |
| [`VRRig`](../components/VRRig.md) | host | readwrite | tick | VRRoller: `transform()` read, `position` written |
| [`Input`](../components/Input.md) | host | read | tick | DesktopRoller: movement and sprint bits |
| [`Camera`](../components/Camera.md) | host | readwrite | tick | DesktopRoller: `yaw`, `right()` read, `position` written |

## Side effects
- Logs an error at construction on invalid lean parameters.
- Reads ImGui IO (`WantCaptureKeyboard`) in the desktop command.

## Ordering and dependencies
- After [WorldStepperSys](WorldStepperSys.md): poses of this frame's ticks; the command applies to the next frame's ticks.
- VRRoller before [VRControlSys](VRControlSys.md) (hands placed in the moved play area). DesktopRoller after [PlayerControlSys](PlayerControlSys.md) (which turns the camera and would otherwise move it).
- Writes into a world registry from the main thread while no world is stepping.

## Related
[SphereViewSys](SphereViewSys.md), [VRControlSys](VRControlSys.md), [PlayerControlSys](PlayerControlSys.md), [`docs/physical-agents/sphere-roller.md`](../../physical-agents/sphere-roller.md), [folder `src/view/`](../folders/src-view.md).
