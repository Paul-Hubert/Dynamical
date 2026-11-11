# VRControlSys

> **Kind:** system · **Registry name:** `VRControl` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/systems/system_list.h`](../../../src/view/systems/system_list.h), [`src/view/systems/vr_control.cpp`](../../../src/view/systems/vr_control.cpp)

## Purpose
VR player controls: left thumbstick locomotion of the play area ([`VRRig`](../components/VRRig.md)) relative to the head direction, right-thumbstick smooth turning, right-trigger box throwing with the hand velocity, and two small kinematic boxes that follow the controllers (drawn and colliding with dynamic bodies).

## Lifecycle
- **Construction / params:** `DEFINE_SYSTEM(VRControlSys)`, no JSON params. Constants: `hand_size = 0.08`, `move_speed = 4` m/s, `turn_speed = 1.5` rad/s.
- **preinit:** `reg.ctx().emplace<VRHands>()`.
- **init:**
  1. `get<VRRig>()` (required); if `std::shared_ptr<TerrainGenerator>` exists, `rig.position.y = surfaceHeight(0, 0)`.
  2. Return if `VRInput` is missing or `!enabled`.
  3. Load `./resources/models/box.gltf`; return if it fails.
  4. Create two hand entities: `Transform(0, identity, scale 0)`, `ModelRenderable(box, blue (0.2,0.4,1) / red (1,0.3,0.2))`, `RigidBody` Kinematic with `half_extent = 0.04`.
- **tick(dt):**
  1. Return if `VRInput` is missing or `!session_running`.
  2. If `focused`:
     - Left stick: if `rig.stick_locomotion`, `|stick| > 0.15` and `head.valid`: forward = world-from-stage rotation * head -Z, flattened; if its length > 0.01, `rig.position += (forward * stick.y + right * stick.x) * 4 * dt`; then (still inside the stick branch) `rig.position.y = surfaceHeight(x, z)` when the generator exists.
     - Right stick: if `|thumbstick.x| > 0.3`, `rig.yaw -= x * 1.5 * dt`.
     - Right trigger rising edge with a valid pose: `spawnBox` of size 0.3, colour (1, 0.8, 0.3), 0.2 m along the hand's -Z, velocity = rig rotation * hand linear velocity * 1.5.
  3. For each created hand: `Transform.position = rig * pose.position`, `rotation = angleAxis(rig.yaw, Y) * pose.rotation`, `scale = 0.08` if `pose.valid` else 0 (hidden), regardless of focus.
- **finish:** None (empty). Hand entities are not destroyed.

## Expected behavior
- Without a headset (or `vr_mode=0`) no hand entities exist and tick returns immediately; init still snaps the rig height.
- When [RollerPlayerSys](RollerPlayerSys.md) (VRRoller) is present it sets `stick_locomotion = false` in init, so the left stick no longer moves the rig (turning and throwing still work).
- Hands are created only if VR was enabled at init; a headset appearing later gets no hands.
- Hand transforms are written in tick; the kinematic bodies follow them in the physics step.
- Rig height snaps to the terrain only while moving with the stick.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`VRHands`](../components/VRHands.md) | host | write +create | preinit | `emplace<VRHands>()` |
| [`VRRig`](../components/VRRig.md) | host | write | init | `position.y` |
| [`TerrainGenerator`](../components/shared_ptr-TerrainGenerator.md) (`std::shared_ptr<TerrainGenerator>`) | host | read | init | `find`, `surfaceHeight(0, 0)` |
| [`VRInput`](../components/VRInput.md) | host | read | init | `find`, `enabled` |
| [`ModelManager`](../components/ModelManager.md) | host | read | init | `load(box.gltf)` |
| [`VRHands`](../components/VRHands.md) | host | readwrite | init | stores the hand entities |
| [`Transform`](../components/Transform.md) | host | write +create | init | hand entities |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | write +create | init | hand entities |
| [`RigidBody`](../components/RigidBody.md) | host | write +create | init | kinematic hand boxes |
| [`VRInput`](../components/VRInput.md) | host | read | tick | session state, head, sticks, trigger, poses, velocity |
| [`VRRig`](../components/VRRig.md) | host | readwrite | tick | `stick_locomotion`, `transform()`, `position`, `yaw` |
| [`TerrainGenerator`](../components/shared_ptr-TerrainGenerator.md) (`std::shared_ptr<TerrainGenerator>`) | host | read | tick | `surfaceHeight` under the rig |
| [`VRHands`](../components/VRHands.md) | host | read | tick | hand entities |
| [`Transform`](../components/Transform.md) | host | write | tick | hand poses; also `+create` for thrown boxes (spawnBox) |
| [`ModelManager`](../components/ModelManager.md) | host | read | tick | spawnBox |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | write +create | tick | spawnBox |
| [`RigidBody`](../components/RigidBody.md) | host | write +create | tick | spawnBox: Dynamic box with `initial_velocity` |

## Side effects
- Creates 2 host entities at init (when VR is enabled) and one entity per trigger press.
- The created RigidBody components lead to Jolt bodies in the host physics systems.

## Ordering and dependencies
- `VRInput` and `VRRig` are emplaced by the renderer (VR input / VR render target), which is constructed before the systems, so init can `get<VRRig>()`.
- After the renderer's `prepare()` updated `VRInput` for this frame (Game loop order).
- After [RollerPlayerSys](RollerPlayerSys.md) VRRoller, which moves the rig (header comment: the hands are placed in the moved play area).
- Before the host physics systems (`RigidBody`, `Physics`) so kinematic hands and new boxes are taken this frame.

## Related
[PlayerControlSys](PlayerControlSys.md) (R moves the rig), [RollerPlayerSys](RollerPlayerSys.md), [DebugMenuSys](DebugMenuSys.md), [`VRHands`](../components/VRHands.md), [folder `src/view/systems/`](../folders/src-view-systems.md).
