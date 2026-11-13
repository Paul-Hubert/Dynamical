# DesktopRoller

`RollerPlayerSys` (`Player::Desktop`) in [`src/view/roller_player.cpp`](../../../src/view/roller_player.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: yes

The desktop player of `vr_roller` (docs/physical-agents/sphere-roller.md): rides a world's steered sphere (the wheel in
`vr_roller`'s world 1). WASD relative to where the camera looks is the command; the camera's eyes sit `eye_height` above
the sphere's lowest point, while PlayerControl turns it with the right mouse button. Same class as
[VRRoller](VRRoller.md).

## Behaviour

- **Constructor**: as VRRoller (same parameters, same lean check).
- **init**: nothing for the desktop player.
- **tick** (nothing without the world or its sphere):
  1. Command (`desktopCommand`), zero without `Input` or `Camera` or while ImGui wants the keyboard: W/S along the
     camera's horizontal forward (from yaw), D/A along `Camera::right()`, normalised, x `stick_speed` with Shift, else
     2/3 of it.
  2. `steerSphere(agent, command)`.
  3. Floor smoothed as VRRoller's.
  4. `Camera::position` = (x, floor + `eye_height`, z) of the sphere.

## Parameters

Same keys as [VRRoller](VRRoller.md); the lean keys and `head_top` are unused here, `eye_height` (1.6 m) places the
eyes.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Worlds](../components/Worlds.md) | context | read | tick | the world of "world" |
| [SphereAgent](../components/SphereAgent.md) | component | readwrite | tick (world registry, steerSphere) | goal_heading and goal_speed from the player's command |
| [Transform](../components/Transform.md) | component | read | tick (world registry) | the sphere's position without a PhysicsPose |
| [PhysicsPose](../components/PhysicsPose.md) | component | read | tick (world registry) | interpolated position with the stepper alpha |
| [RigidBody](../components/RigidBody.md) | component | read | tick (world registry) | half_extent.x: the floor is the sphere's lowest point |
| [WorldStepperState](../components/WorldStepperState.md) | context | read | tick | alpha |
| [Input](../components/Input.md) | context | read | tick (desktopCommand) | FORWARD / BACKWARD / LEFT / RIGHT / SPRINT |
| [Camera](../components/Camera.md) | context | readwrite | tick | yaw and right() for the command; position written eye_height above the floor |

## Side effects

- None besides the components and context above.

## Order and dependencies

After PlayerControl (its mouse look; it moves the camera, this system then overrides the position) and WorldStepper:
both checked by SR.18. SphereView of the same world runs with `camera` false.

## Used in scenes

<!-- generated -->

## Tests

- `SR.18`, `SR.19`, `SR.20` in `tests/onnx/vr_roller.cpp`.
