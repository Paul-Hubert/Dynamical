# VRActions

`VRActions` in [`src/renderer/vr_render/vr_input.cpp`](../../../src/renderer/vr_render/vr_input.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by [VRRender](service-VRRender.md)) · Parameters: no

The OpenXR actions of the controllers: one action set "gameplay" with a hand pose, trigger, grip, thumbstick,
primary button and thumbstick click per hand, and the head pose. Its results go in the `VRInput` context
variable, the renderer side's raw state of the headset that `VRControl` and the player input (`VRPlayerInput`)
read.

## Behaviour

- **Constructor** (in `VRRender`'s constructor): emplaces `VRInput` and sets `enabled` to whether a headset was
  found; returns there without VR. With VR: creates the action set and the six actions with the left and
  right hand subaction paths, suggests bindings for `oculus/touch_controller` (primary X left, A right),
  `valve/index_controller` (A on both) and `khr/simple_controller` (pose and select as trigger) — runtimes that
  reject a profile are ignored —, an action space per hand (grip pose), a `VIEW` reference space, and attaches
  the action set to the session.
- **sync(time)** (`VRRender::prepare`, after `xrBeginFrame`, at the predicted display time): with VR,
  - `head` from the view space located in the stage space;
  - when the session is not `focused`: every hand reset to its defaults, and nothing else;
  - else `xrSyncActions`, then per hand `trigger` (0 when inactive) and `trigger_pressed` (rising edge across
    0.8, from last frame's value), `grip`, `thumbstick`, `primary` and `primary_pressed` (rising edge),
    `thumbstick_click`, `pose` (valid when position and orientation are), `linear_velocity` and
    `angular_velocity` (stage space, 0 when invalid); `pose.valid` false when the hand cannot be located.
- **Destructor**: destroys the spaces and the action set.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| VRInput | context | readwrite+create | constructor (emplace, `enabled`), sync | head and hand poses, buttons, sticks; the previous `trigger` and `primary` give the rising edges, `focused` (written by `VRRender`) gates the hands |

## Side effects

- OpenXR: action set, actions, suggested bindings, action and reference spaces, `xrSyncActions` and state
  queries once a frame.
- Log: failures of OpenXR calls through `xrCheckResult`.

## Order and dependencies

- Runs inside `VRRender::prepare`, so `VRInput` is fresh before the systems' pre tick.

## Used in scenes
<!-- generated -->

## Tests

- None (not run in a headset). `PLAYER.*` ([`tests/unit/player.cpp`](../../../tests/unit/player.cpp)) drive the
  VR player with scripted tracking, downstream of `VRInput`.
