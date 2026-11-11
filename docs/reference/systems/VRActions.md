# VRActions

> **Kind:** service · **Registry name:** — · **Scope:** n/a (host registry) · **Lane:** per-frame call from `VRRender::prepare` (after `xrBeginFrame`) · **Needs renderer:** yes
> **Source:** [`src/renderer/vr_render/vr_input.h`](../../../src/renderer/vr_render/vr_input.h), [`src/renderer/vr_render/vr_input.cpp`](../../../src/renderer/vr_render/vr_input.cpp)

## Purpose
OpenXR input: creates the `gameplay` action set (hand pose, trigger, grip, thumbstick, primary button for both hands), suggests bindings for common controllers, and each VR frame writes the head pose and controller states into the `VRInput` context variable in stage space.

## Lifecycle
- **Construction / params:** `VRActions(Context&, entt::registry&)` from the `VRRender` constructor. Emplaces `VRInput` with `enabled = ctx.vr.enabled()`; returns if VR is disabled. Otherwise creates the action set, the `/user/hand/left|right` paths, five actions with both hands as subaction paths, suggested bindings:
  - `oculus/touch_controller`: grip pose, trigger value, squeeze value, thumbstick, primary `x/click` (left) and `a/click` (right).
  - `valve/index_controller`: same, primary `a/click` on both hands.
  - `khr/simple_controller`: grip pose, trigger = `select/click`.
  Rejected profiles are ignored. Creates one action space per hand and a `VIEW` reference space, attaches the action set to the session.
- **sync(XrTime time):** returns if VR is disabled.
  1. Locates the view space in the reference space: on success writes `VRInput::head` (valid only when position and orientation are valid).
  2. If `VRInput::focused` is false: resets both hands to default `Hand` and returns.
  3. `xrSyncActions`, then per hand: `trigger` (0 when inactive) and `trigger_pressed` = rising edge across 0.8; `grip`; `thumbstick` (zero when inactive); `primary` and `primary_pressed` = rising edge; locates the hand space with velocity: on success `pose` and `linear_velocity` (zero if not valid), otherwise `pose.valid = false` (previous position kept).
- **finish:** none; destructor destroys the spaces and the action set.

## Expected behavior
- `VRInput` always exists in rendered runs, `enabled` false without a headset.
- `trigger_pressed` / `primary_pressed` are true for exactly one VR frame per press (while focused). Losing focus clears all hand state, so a press held across a focus change fires again.
- Head pose is updated even when unfocused; hands are not.
- All poses and velocities are in the OpenXR stage (or local) space, not world space; `VRRig` maps them to world.
- When the session is not running or the frame could not be begun, `sync` is not called and `VRInput` keeps last frame's values.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`VRInput`](../components/VRInput.md) | host | write +emplace_ctx | construction | `enabled` |
| [`VRInput`](../components/VRInput.md) | host | readwrite | prepare (sync) | Reads `focused` and previous `trigger`/`primary`; writes `head`, `hands[0..1]` |

## Side effects
- OpenXR calls: action/space creation, `xrSuggestInteractionProfileBindings`, `xrAttachSessionActionSets`; per frame `xrLocateSpace`, `xrSyncActions`, `xrGetActionState*`.
- Logs OpenXR errors via `xrCheckResult`.

## Ordering and dependencies
- Called after `xrWaitFrame`/`xrBeginFrame` with the frame's predicted display time, before the systems tick, so `VRControlSys` and roller/player systems read fresh input.
- `VRRender::pollEvents` writes `focused` just before `sync` reads it.
- Main thread only.

## Related
[VRRender](VRRender.md), [Renderer](Renderer.md), `VRInput` / `VRRig` in `src/logic/components/vr.h`, [`src/renderer/vr_render/`](../folders/src-renderer-vr_render.md).
