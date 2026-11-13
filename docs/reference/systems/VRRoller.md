# VRRoller

`RollerPlayerSys` (`Player::VR`) in [`src/view/roller_player.cpp`](../../../src/view/roller_player.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: yes

The VR player of `vr_roller` (docs/physical-agents/sphere-roller.md): their locomotion is a world's trained, steered
sphere. The command of the sphere comes from the left stick or, with the stick at rest, from how far the player leans
(once A has been pressed); the play area stands on the sphere's lowest point, smoothed in height. The same class is
[DesktopRoller](DesktopRoller.md) for the desktop player.

## Behaviour

- **Constructor**: reads the parameters; when not 0 < `lean_deadzone` < `lean_range`, logs an error and uses 0.01 and
  0.08.
- **init**: `VRRig::stick_locomotion = false` (VRControl no longer moves the play area with the left stick; it still
  turns it with the right stick).
- **tick** (nothing without the world or its sphere, the first entity with `SphereAgent`, `Transform`, `RigidBody`):
  1. Command (`vrCommand`), zero without a running, focused session and a valid head:
     - A pressed this frame: the lean origin = the top of the head (`head.position + head.rotation x head_top`, stage
       space); leaning starts;
     - left stick past `deadzone`: along the head's horizontal forward / right in world space, speed
       `stick_speed x (deflection - deadzone) / (1 - deadzone)`;
     - else, when leaning: the horizontal offset of the top of the head from the origin gives the direction, and
       `leanSpeed(offset, lean_deadzone, lean_range, lean_speed)` the speed (0 within the dead zone, logarithmic,
       `lean_speed` from `lean_range` on), turned to the world with the play area.
  2. `steerSphere(agent, command)`: `goal_heading` (when moving) and `goal_speed`.
  3. Floor = the interpolated (stepper alpha) sphere centre's y minus its radius; snapped on the first frame and after
     a jump of more than 2 m (respawn), else eased at `floor_smoothing`.
  4. `VRRig::position` = (x, floor, z) of the sphere.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `world` | int | 0 | world of the sphere |
| `deadzone` | float | 0.15 | stick dead zone |
| `stick_speed` | float | 3.0 | m/s at full deflection (training's top target speed) |
| `lean_speed` | float | 3.0 | m/s from `lean_range` on |
| `lean_range` | float | 0.08 | m of lean for full speed |
| `lean_deadzone` | float | 0.01 | m of lean under which the sphere stops |
| `head_top` | [x, y, z] | [0, 0.11, 0.09] | the top of the head in OpenXR view space (over the neck's yaw axis) |
| `eye_height` | float | 1.6 | unused by the VR player |
| `floor_smoothing` | float | 10.0 | 1/s |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [VRRig](../components/VRRig.md) | context | readwrite | init, tick (vrCommand) | init turns stick_locomotion off (VRControl no longer moves it); tick moves position onto the sphere's floor; transform() turns the command to the world |
| [Worlds](../components/Worlds.md) | context | read | tick | the world of "world" |
| [SphereAgent](../components/SphereAgent.md) | component | readwrite | tick (world registry, steerSphere) | goal_heading and goal_speed from the player's command |
| [Transform](../components/Transform.md) | component | read | tick (world registry) | the sphere's position without a PhysicsPose |
| [PhysicsPose](../components/PhysicsPose.md) | component | read | tick (world registry) | interpolated position with the stepper alpha |
| [RigidBody](../components/RigidBody.md) | component | read | tick (world registry) | half_extent.x: the floor is the sphere's lowest point |
| [WorldStepperState](../components/WorldStepperState.md) | context | read | tick | alpha |
| [VRInput](../components/VRInput.md) | context | read | tick (vrCommand) | session_running, focused, head pose, left thumbstick, right primary (A) press |

## Side effects

- None besides the components and context above.

## Order and dependencies

After WorldStepper (the poses of this frame's ticks), before VRControl (which places the hands in the moved play
area): both checked by SR.18. The steer is read by the sphere's world at its next tick.

## Used in scenes

<!-- generated -->

## Tests

- `SR.18 vr_roller: the VR player on the small sphere and the desktop player on the wheel ...` (order, parameters,
  scene variables), `SR.19`, `SR.20` (scenario suite: the shipped policy follows commands) in `tests/onnx/vr_roller.cpp`.
