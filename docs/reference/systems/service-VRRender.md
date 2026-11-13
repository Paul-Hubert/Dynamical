# VRRender

`VRRender` in [`src/renderer/vr_render/vr_render.cpp`](../../../src/renderer/vr_render/vr_render.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by [Renderer](service-Renderer.md)) · Parameters: no (`vr_mode` setting, the scene's `renderer.vr`)

The headset render target, drawn alongside the desktop every frame (it does not replace it): the OpenXR frame
loop (session events, `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame`), the render pass and framebuffers of the
eye swapchains (`VRRenderpass`), one command buffer per frame and one view UBO and view set per frame and eye.
It owns [VRActions](service-VRActions.md) (the controllers) and with ray tracing a `SunVisibility` of the eyes'
extents. When no headset was found (`ctx.vr.enabled()` false) it only makes the context variables.

## Behaviour

- **Constructor**: emplaces the `VRRig` context variable (default: at the origin, yaw 0) and makes `VRActions`
  (which emplaces `VRInput`), whether VR is enabled or not. Then, with VR: command pool and buffers, a
  descriptor pool, per frame and eye a view set with a mapped `ViewUBO` buffer (binding 0, the rest by
  `Context::writeViewSet`), and with ray tracing a `SunVisibility`.
- **prepare(frame)** (`Renderer::prepare`): with VR, `pollEvents()`: `READY` begins the session (log `VR session
  started`), `STOPPING` ends it (`VR session stopped`), `EXITING` / `LOSS_PENDING` drop it (warning `VR session
  lost`: the desktop keeps running), instance loss (warning `VR runtime is shutting down`); writes
  `VRInput::focused` (state `FOCUSED`) and `VRInput::session_running`. With a running session, `xrWaitFrame`
  (blocks until the headset wants a frame, zone "Wait for the headset's frame"), `xrBeginFrame`, and
  `VRActions::sync` at the predicted display time. `active()`: a frame was begun and `shouldRender`.
- **acquire()** (main thread, before recording): acquires and waits for an image of each eye swapchain.
- **updateView(frame, time)**: locates the views (`xrLocateViews` at the predicted display time, as late as
  possible); per eye, world-from-eye = `VRRig::transform()` (rig position and yaw) x the eye's stage pose;
  `ViewUBO`: Vulkan projection of the asymmetric field of view (near 0.05, far 3000 m) x inverse world-from-eye,
  the eye position (w = time), `tree_shade` (`treeShadeView`), the inverse, the previous view projection and
  position of the same eye, `lighting` (`lightingView`, the VR tier); rewrites the eye's view set.
- **record(frame, passes)** (taskflow worker, concurrently with the desktop): GPU zone "Headset", per eye
  ("Headset eye 0/1"): like the desktop, with ray tracing the scene into `SunVisibility`, "Sun rays (headset)",
  then "Composite (headset)" in the eye's render pass; without, the passes that support VR directly; overlay
  passes last. Zones `record <pass> (headset)`, `<pass> (headset)`.
- **endFrame()** (after the graphics submission): releases the eye images and submits one projection layer when
  the frame was rendered, `xrEndFrame` in any case once a frame was begun; marks a Tracy frame `Headset`.
- **Destructor**: destroys `VRActions`, requests the session's exit if it runs, destroys the pools.

## Parameters

None of its own: `Settings::vr_mode` (0 desktop only, 1 VR alongside when a headset is found) and the scene's
`renderer.vr` (false forces `vr_mode` 0).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| VRRig | context | read+create | constructor (emplace), updateView | the stage's place and yaw in the world: the eyes' world poses |
| VRInput | context | write | prepare (`pollEvents`) | `focused` and `session_running` from the session's state |
| RenderSettings | context | read | updateView (`treeShadeView`, `lightingView`) | `tree_shade` strength and the VR lighting tier into the eyes' `ViewUBO` |

`VRInput` is created and its poses and buttons written by [VRActions](service-VRActions.md).

## Side effects

- OpenXR: session begin / end / exit request, frame wait / begin / end, swapchain image acquire / wait / release,
  view location.
- GPU: per-frame and per-eye uniform buffers, descriptor sets, the headset command buffer.
- Log: session started / stopped (info), session lost, runtime shutting down (warnings).
- Profiler: `DY_FRAME_NAMED("Headset")`, CPU and GPU zones per pass and eye.

## Order and dependencies

- `prepare` writes `VRInput` before the systems tick (`VRControl`, the player systems read it); `VRRig` is read in
  `updateView`, after the systems moved it (`VRControl`, `DesktopView`).
- Records concurrently with [ClassicRender](service-ClassicRender.md).

## Used in scenes
<!-- generated -->

## Tests

- None run in a headset (CLAUDE.md: "Not run in a headset"). Without a runtime the constructor path that only
  emplaces `VRRig` and `VRInput` runs in every visual test (see [Renderer](service-Renderer.md)).
