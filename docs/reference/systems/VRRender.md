# VRRender

> **Kind:** service · **Registry name:** — · **Scope:** n/a (host registry) · **Lane:** per-frame call from `Renderer::prepare` / `Renderer::render` · **Needs renderer:** yes
> **Source:** [`src/renderer/vr_render/vr_render.h`](../../../src/renderer/vr_render/vr_render.h), [`src/renderer/vr_render/vr_render.cpp`](../../../src/renderer/vr_render/vr_render.cpp), [`src/renderer/vr_render/renderpass.h`](../../../src/renderer/vr_render/renderpass.h), [`src/renderer/vr_render/renderpass.cpp`](../../../src/renderer/vr_render/renderpass.cpp)

## Purpose
Headset render target, running alongside the desktop target. Drives the OpenXR frame loop (session events, `xrWaitFrame`, `xrBeginFrame`, `xrEndFrame`), owns `VRActions`, and per frame writes one `ViewUBO` per eye from the located views and the `VRRig` context variable (stage space -> world), then records the scene passes once per eye into one command buffer.

## Lifecycle
- **Construction / params:** `VRRender(Context&, entt::registry&)` from the Renderer. Always emplaces a default `VRRig` and creates `VRActions` (which emplaces `VRInput`). If VR is disabled (`ctx.vr.enabled()` false) stops there. Otherwise allocates per frame a command buffer and per eye a mapped `ViewUBO` and descriptor set. `VRRenderpass`: color in the OpenXR swapchain format with final layout `COLOR_ATTACHMENT_OPTIMAL`, depth `D32_SFLOAT`/`D16_UNORM`, one depth image and framebuffer per eye and swapchain image.
- **prepare(frame_index):** clears `frame_begun`/`should_render`; returns if VR is disabled. `pollEvents()`:
  - `READY` -> `xrBeginSession` (session_running = true), `STOPPING` -> `xrEndSession` (false), `EXITING` / `LOSS_PENDING` -> false (logged, desktop continues); after each state-change event `VRInput::focused = (state == FOCUSED)`. `INSTANCE_LOSS_PENDING` -> session_running = false.
  - Writes `VRInput::session_running`.
  Returns if the session is not running. `xrWaitFrame` (returns on failure), `xrBeginFrame` (returns on failure), `frame_begun = true`, `should_render = frame_state.shouldRender`, then `VRActions::sync(predictedDisplayTime)`.
- **acquire():** if `active()` (`frame_begun && should_render`), acquires and waits (infinite timeout) one image per eye swapchain.
- **updateView(frame_index, time):** if active, `xrLocateViews` at the predicted display time in the reference space; per eye `world_from_eye = VRRig::transform() * pose`, `view_projection = projection_fov(fov, 0.05, 3000) * inverse(world_from_eye)` (Vulkan clip space, asymmetric FOV), `position = (eye world position, time)`.
- **record(frame_index, passes):** on a taskflow worker. For each eye: render pass on the acquired image's framebuffer, clear (0.45, 0.6, 0.8, 1), viewport/scissor, `record` of passes supporting `VR` with `RenderView { VR, frame_index, eye, set, extent }`.
- **endFrame():** after the graphics submit; if `frame_begun`: when `should_render`, releases the eye images and submits one projection layer with the located poses/FOVs; always calls `xrEndFrame` (with zero layers when not rendering).
- **finish:** none; destructor destroys `VRActions`, requests session exit if running, destroys pools.

## Expected behavior
- VR disabled (vr_mode 0, no runtime, no headset): `VRRig` and `VRInput` (with `enabled = false`) still exist; `active()` is always false.
- When the runtime says `shouldRender = false`, the frame is still begun and ended (no layers) to keep the session paced.
- `xrWaitFrame` blocks to the headset's display cadence; with a headset the desktop swapchain uses mailbox/immediate present so it does not add a second throttle.
- Eye positions in the UBO are in world space: moving `VRRig` moves the whole play area.
- `VRInput::focused` changes only on session state change events.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`VRRig`](../components/VRRig.md) | host | write +emplace_ctx | construction | Default rig |
| [`VRInput`](../components/VRInput.md) | host | write | prepare | `focused`, `session_running` |
| [`VRRig`](../components/VRRig.md) | host | read | render (updateView) | `transform()` (position, yaw) |

`VRInput` emplacement and controller fields: see [VRActions](VRActions.md).

## Side effects
- OpenXR calls: `xrPollEvent`, `xrBeginSession`, `xrEndSession`, `xrWaitFrame` (blocking), `xrBeginFrame`, `xrAcquireSwapchainImage`, `xrWaitSwapchainImage` (blocking), `xrLocateViews`, `xrReleaseSwapchainImage`, `xrEndFrame`, `xrRequestExitSession` at destruction.
- Logs session start/stop/loss.
- Writes mapped UBO memory; records commands.

## Ordering and dependencies
- `prepare` (in `Renderer::prepare`) runs before the systems, so `VRControlSys` and others see this frame's `VRInput`.
- `updateView` runs after the systems, so a `VRRig` moved this frame is used.
- `record` runs concurrently with `ClassicRender::record`; read-only on the registry.
- `endFrame` must follow the graphics submit (OpenXR requires the images' rendering to be submitted before release).

## Related
[VRActions](VRActions.md), [Renderer](Renderer.md), [ClassicRender](ClassicRender.md), [`src/renderer/vr_render/`](../folders/src-renderer-vr_render.md), [`src/renderer/context/`](../folders/src-renderer-context.md) (`VRContext`).
