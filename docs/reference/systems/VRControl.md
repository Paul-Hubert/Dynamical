# VRControl

`VRControlSys` in [`src/view/systems/vr_control.cpp`](../../../src/view/systems/vr_control.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: no

The simple VR player of the non-player scenes: moves the play area ([VRRig](../components/VRRig.md)) with the left
stick, turns it with the right stick, throws boxes with the right trigger, and shows two small boxes on the controllers
that are kinematic Jolt bodies (they push the boxes around). The physical VR player of `player_vr` is another set of
systems (docs/players.md).

## Behaviour

- **preinit**: emplaces [VRHands](../components/VRHands.md).
- **init**:
  1. Play area on the ground at the origin (`TerrainGenerator::surfaceHeight(0, 0)`), when there is a generator.
  2. Without `VRInput` or with `enabled` false (no headset): stops here.
  3. Loads `./resources/models/box.gltf` (stops if missing) and makes the two hands: `Transform` (scale 0),
     `ModelRenderable` (blue left, red right), kinematic `RigidBody` of 8 cm.
- **tick** (only while `VRInput::session_running`):
  - While `focused`:
    - left stick beyond 0.15 and `rig.stick_locomotion` (VRRoller turns it off) and a valid head: moves the rig along
      the head's horizontal forward / right in world space at 4 m/s full deflection, then onto the ground;
    - right stick x beyond 0.3: `rig.yaw -= x * 1.5 rad/s * dt`;
    - right trigger press with a valid pose: `spawnBox` 0.3 m, 20 cm ahead of the controller, velocity = 1.5 x the
      controller's linear velocity in world space.
  - Always: the hands' `Transform` = controller pose in world space (rig transform), rotation = rig yaw x pose
    rotation, scale 0.08 when the pose is valid, 0 otherwise. The kinematic bodies follow the `Transform`.
- **finish**: nothing (the hand entities stay).

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [VRHands](../components/VRHands.md) | context | readwrite+create | preinit (emplace), init, tick | the two hand entities, made in init, read every tick |
| [VRRig](../components/VRRig.md) | context | readwrite | init, tick | init puts the play area on the ground at the origin; tick moves position (stick, onto the ground) and yaw (turn) |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init, tick | surfaceHeight() under the play area; optional |
| [VRInput](../components/VRInput.md) | context | read | init, tick | enabled / session_running / focused, head pose, thumbsticks, right trigger, hand poses and velocity |
| [ModelManager](../components/ModelManager.md) | context | readwrite | init, tick (spawnBox) | load("./resources/models/box.gltf") |
| [ModelC](../components/ModelC.md) | component | write+create | init, tick (ModelManager::load) | the box model, made on its first load |
| [Transform](../components/Transform.md) | component | readwrite+create | init, tick | hand entities made in init; tick writes their position, rotation, scale (0 when untracked); thrown boxes |
| [ModelRenderable](../components/ModelRenderable.md) | component | write+create | init, tick (spawnBox) | blue left hand, red right hand, yellow thrown boxes |
| [RigidBody](../components/RigidBody.md) | component | write+create | init, tick (spawnBox) | kinematic 8 cm boxes for the hands; dynamic 0.3 m thrown boxes |

## Side effects

- Hands are only made when VRInput is enabled and box.gltf loads; the RigidBody system gives them kinematic Jolt bodies that push dynamic bodies around.
- Each press of the right trigger spawns a box 20 cm ahead of the controller with 1.5x the hand's velocity (a new entity and Jolt body per press, never destroyed).

## Order and dependencies

- Needs `VRRig`, `VRInput` (renderer's VR target and `VRActions`) and `ModelManager`.
- After VRRoller in `vr_roller` (VRRoller moves the play area first so the hands are placed in the moved area; SR.18).
- Before RigidBody / Physics so that new boxes get their bodies and the kinematic hands move this frame.

## Used in scenes

<!-- generated -->

## Tests

- `T2.4 base system list ...` (stub), `SR.18 vr_roller: ...` (`VRRoller` before `VRControl`). Not run in a headset by
  the tests.
