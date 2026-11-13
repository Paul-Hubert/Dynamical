# VRBody

`VRBodySys` in [`src/logic/player/vr_player.cpp`](../../../src/logic/player/vr_player.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `VRPlayerInput`, before `PlayerLegs` · Parameters: no

Unity's VRPlayer: the VR player's body is at the head. It follows the head's motion in the room, the view axis turns the play area around it, the legs stand it at the height the head is held at, and the walk follows the head. While the session has no focus, and for two frames after it comes back, poses are held so that a system menu never moves the body.

## Behaviour

- **preinit**: gets the `PhysicsWorld` (created when missing).
- **tick**, for every entity with `VRBody`, `VRTracking`, `PlayerView`, `PlayerLegs`, `Transform`:
  1. **Focus** (Unity's TrackFocus): when focus comes back after having been seen, `refocus_frame` = 1, then counts up
     to 3 and back to 0. `pose_hold = !focused || refocus_frame in {1, 2}`.
  2. **Move** (when the head pose is valid): held: on refocus frame 2 the head is only re-based (`head` = tracked
     position). The first pose ever only places the play area (`head` set, `tracked` = true). Otherwise the head's
     motion since last frame, turned by the play area's yaw, is added to the `Transform`, to both positions of the
     `PhysicsPose` when there is one, and to the Jolt body (`SetPosition`, activated).
  3. **Turn**: with a rate input (not a mouse), `yaw -= viewAxis.x x turn_speed x dt`.
  4. **Height**: `PlayerLegs::height = max(standing_height + head.y - 1.8, 0.3)`.
  5. **View**: the head's forward turned by the play area gives `PlayerView` yaw (when it is not vertical) and pitch.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [ScriptedInput](../components/ScriptedInput.md) | component | read | helper playerInput | answers for the player when present (tests, scripts) |
| [MouseKeyboardInput](../components/MouseKeyboardInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [ControllersInput](../components/ControllersInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [VRPlayerInput](../components/VRPlayerInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit, tick | the player's Jolt body is moved by the head's motion (`SetPosition`, activates it) |
| [VRBody](../components/VRBody.md) | component | readwrite | tick | focus state machine (pose_hold, was_focused, seen_focus, refocus_frame), last head position, tracked, yaw turned by the view axis |
| [VRTracking](../components/VRTracking.md) | component | read | tick | focused, head pose |
| [PlayerView](../components/PlayerView.md) | component | write | tick | yaw and pitch from the head's forward turned by the play area |
| [PlayerLegs](../components/PlayerLegs.md) | component | write | tick | `height` = max(standing_height + head.y - 1.8, 0.3) |
| [Transform](../components/Transform.md) | component | readwrite | tick | position moved by the head's motion in the room (turned by the play area) |
| [PhysicsPose](../components/PhysicsPose.md) | component | readwrite | tick | when present: both interpolated positions moved by the same delta |
| [RigidBody](../components/RigidBody.md) | component | read | tick (helper playerBodyId) | the Jolt body id of the player |

## Side effects

- Teleports the player's Jolt body by the head's motion each frame (`BodyInterface::SetPosition`, activated).

## Order and dependencies

After `VRPlayerInput` (or a test writing `VRTracking`), before `PlayerLegs` (which uses `height` and the view) and `VRHands`; `PlayerViewpoint` reads `head` and `yaw` after `Physics` to place the play area.

## Used in scenes

<!-- generated -->

## Tests

`PLAYER.6` (the body follows the head, the play area turns, poses are held without focus); `PLAYER.4`, `PLAYER.5`, `PLAYER.7` run it with the VR player; [`tests/unit/player.cpp`](../../../tests/unit/player.cpp).
