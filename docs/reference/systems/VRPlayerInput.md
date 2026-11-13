# VRPlayerInput

`VRPlayerInputSys` in [`src/logic/player/vr_player.cpp`](../../../src/logic/player/vr_player.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, first of the VR player systems (before `VRBody`) · Parameters: no

The headset's state, written by the renderer into the `VRInput` context variable, becomes the components of each VR player: its `VRTracking` (head and hands in stage space) and its `VRPlayerInput` (the controllers as a `GenericPlayerInput`). The rest of the player systems never read `VRInput`, so a test can write `VRTracking` and a `ScriptedInput` instead.

## Behaviour

- **tick**: returns when there is no `VRInput` context variable. Adds `dt` to its own play time. For every entity with
  `VRBody`, `VRTracking` and `VRPlayerInput`:
  - `VRTracking`: `focused = session_running && focused`; `head`; per hand the pose, linear and angular velocity,
    grip and trigger.
  - `VRPlayerInput`: `focused`, both thumbsticks, the left thumbstick click, the right controller's primary button (A),
    both triggers.
  - `last_action` = play time when focused and any control is in use (a stick past half its travel, the click, A, a
    trigger past 0.5): `playerInput()` lets the device used last speak for the player.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [VRInput](../components/VRInput.md) | context | read | tick | session state, head pose, controller poses, velocities, grip, trigger, sticks and buttons; nothing done without it |
| [VRBody](../components/VRBody.md) | component | read | tick | view filter: only VR players |
| [VRTracking](../components/VRTracking.md) | component | write | tick | focused, head, both hands' pose, velocities, grip, trigger overwritten |
| [VRPlayerInput](../components/VRPlayerInput.md) | component | write | tick | focused, sticks, left stick click, right A, triggers overwritten; `last_action` set to the play time when a control is used |

## Side effects

- None beyond the components (keeps its own play time, summed dt, for `last_action`).

## Order and dependencies

After the renderer has written `VRInput` (the renderer's `prepare()` precedes the systems), before `VRBody`, `PlayerLegs`, `VRHands` and `Grab`, which read what it writes.

## Used in scenes

<!-- generated -->
- [`player_vr.scene.json`](../../../resources/scenes/player_vr.scene.json): `host.pre`

## Tests

Not run by a unit test (they write `VRTracking` themselves); the `VRPlayerInput` component's answers are checked in `MOUNT.1` ([`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp)). Capture [`player_vr.capture.json`](../../../tests/capture/player_vr.capture.json) runs it without a headset.
