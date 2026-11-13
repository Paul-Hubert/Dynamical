# PlayerViewpoint

`PlayerViewpointSys` in [`src/logic/player/player_body.cpp`](../../../src/logic/player/player_body.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, last: after `Physics` · Parameters: no

Places the views on the player once the physics has moved it: the desktop `Camera` at the eyes of the `DesktopView` player (or behind them with `ThirdPerson`), and the VR play area (`VRRig`) so that the headset's head lands on the VR player's body.

## Behaviour

- **preinit**: emplaces a `Camera` context variable when there is none.
- **tick**:
  - For every `DesktopView` + `Transform` + `PlayerView`: `Camera::position` = the eyes, plus with a `ThirdPerson`
    `(0, height, 0) - forward` times `distance`; `Camera::yaw`, `pitch` = the view's. (With several, the last wins.)
  - When a `VRRig` context variable exists, for every `Transform` + `VRBody` + `VRTracking`: `VRRig::yaw` = the play
    area's, `position = eyes - rotation x head`, `stick_locomotion = false` (VRControl no longer moves the rig).

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Camera](../components/Camera.md) | context | write+create | preinit, tick | created when missing; position, yaw, pitch from the DesktopView player |
| [VRRig](../components/VRRig.md) | context | write | tick | when present: yaw, position (so the head lands on the body), stick_locomotion = false |
| [DesktopView](../components/DesktopView.md) | tag | read | tick | the player the camera follows |
| [Transform](../components/Transform.md) | component | read | tick | the eyes |
| [PlayerView](../components/PlayerView.md) | component | read | tick | yaw, pitch, forward |
| [ThirdPerson](../components/ThirdPerson.md) | component | read | tick | distance and height of the camera behind the eyes |
| [VRBody](../components/VRBody.md) | component | read | tick | play area yaw and the last head position |
| [VRTracking](../components/VRTracking.md) | component | read | tick | view filter only |

## Side effects

- None beyond the context variables.

## Order and dependencies

After `Physics` (the `Transform`s it reads are this frame's interpolated poses) and before the renderer reads the camera and the rig.

## Used in scenes

<!-- generated -->
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.pre`
- [`player_fps.scene.json`](../../../resources/scenes/player_fps.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`
- [`player_vr.scene.json`](../../../resources/scenes/player_vr.scene.json): `host.pre`

## Tests

Run by every test of [`tests/unit/player.cpp`](../../../tests/unit/player.cpp) and [`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp); the camera is not checked there.
