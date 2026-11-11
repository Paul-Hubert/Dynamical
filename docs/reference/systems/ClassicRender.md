# ClassicRender

> **Kind:** service · **Registry name:** — · **Scope:** n/a (host registry) · **Lane:** per-frame call from `Renderer::prepare` / `Renderer::render` · **Needs renderer:** yes
> **Source:** [`src/renderer/classic_render/classic_render.h`](../../../src/renderer/classic_render/classic_render.h), [`src/renderer/classic_render/classic_render.cpp`](../../../src/renderer/classic_render/classic_render.cpp), [`src/renderer/classic_render/renderpass.h`](../../../src/renderer/classic_render/renderpass.h), [`src/renderer/classic_render/renderpass.cpp`](../../../src/renderer/classic_render/renderpass.cpp)

## Purpose
Desktop window render target. Owns the render pass and framebuffers (`ClRenderpass`: swapchain color + one depth image per swapchain image), one command buffer, view UBO/descriptor set and acquire semaphore per frame in flight, and one present semaphore per swapchain image. Writes the `ViewUBO` from the `Camera` context variable, records the scene passes and presents. Optionally copies the frame to a host buffer for captures.

## Lifecycle
- **Construction / params:** `ClassicRender(Context&, entt::registry&)` from the Renderer. Render pass: color (swapchain format, clear, store, final `PRESENT_SRC`) + depth (`D32_SFLOAT` or `D16_UNORM`, clear). Clear color (0.45, 0.6, 0.8, 1), depth 1.
- **prepare(frame_index):** `acquired = false`; if the `Input` context variable exists and `window_showing` is false, returns false. Otherwise `Swapchain::acquire` with the frame's acquire semaphore (unbounded wait; suboptimal results ignored), `acquired = true`.
- **updateView(frame_index, time):** `get<Camera>()`; `view_projection = getProjection(width / height) * getView()`, `position = (camera.position, time)` into the mapped UBO.
- **record(frame_index, passes):** on a taskflow worker. Begins the one-time command buffer and the render pass on the acquired image's framebuffer, sets the full viewport/scissor, calls `record` of every pass whose `supports(Desktop)` is true with `RenderView { Desktop, frame_index, eye 0, set, extent }`, ends the pass, then records the capture copy if requested.
- **present():** if acquired, presents with the image's present semaphore; clears `acquired`.
- **requestCapture / readCapture:** request fails unless the swapchain images have `TRANSFER_SRC` and an image is acquired; allocates/grows a GPU-to-CPU buffer (width x height x 4). The copy transitions `PRESENT_SRC` -> `TRANSFER_SRC` -> `PRESENT_SRC`. `readCapture` invalidates and converts BGRA/RGBA to tightly packed RGB8.
- **finish:** none; destructor destroys semaphores, pool, descriptor pool (render pass and depth images in `ClRenderpass`).

## Expected behavior
- Hidden/minimized window (`Input::window_showing == false`): no acquire, `active()` false, nothing recorded or presented for the desktop that frame.
- Without an `Input` context variable the image is always acquired.
- `Camera` must exist whenever the target is active (`get`, not `find`); scenes provide it through `PlayerControlSys`.
- The window is not resizable; the swapchain is never recreated.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Input`](../components/Input.md) | host | read | prepare | `window_showing` (`find`) |
| [`Camera`](../components/Camera.md) | host | read | render (updateView) | `position`, `fov`, `near_plane`, `far_plane`, `yaw`, `pitch` through `getProjection`/`getView` |

## Side effects
- `vkAcquireNextImageKHR` (may block), `vkQueuePresentKHR`.
- Writes mapped UBO memory; records commands; capture readback into host memory.

## Ordering and dependencies
- `prepare` runs before the systems tick; `updateView` and `record` after them, so the camera moved by `PlayerControlSys` this frame is used.
- `record` runs in parallel with `VRRender::record`; it only touches its own command buffer and calls read-only pass `record`s.

## Related
[Renderer](Renderer.md), [VRRender](VRRender.md), [UIPass](UIPass.md), [`src/renderer/classic_render/`](../folders/src-renderer-classic_render.md).
