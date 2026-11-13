# ClassicRender

`ClassicRender` in [`src/renderer/classic_render/classic_render.cpp`](../../../src/renderer/classic_render/classic_render.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by [Renderer](service-Renderer.md)) · Parameters: no (`gpu_times` on the command line)

The desktop render target: the swapchain's render pass and framebuffers (`ClRenderpass`), one command buffer,
one view UBO and one view set per frame in flight, the acquire / present semaphores, the frame capture of the
shots, and the per-pass GPU timings of `gpu_times=true`. With ray tracing it owns a `SunVisibility` (the scene is
drawn into two attachments, the sun's rays traced and filtered, then composited into the window's image).

## Behaviour

- **Constructor**: command pool and `NUM_FRAMES` command buffers; a descriptor pool and per frame a view set
  (`ctx.view_layout`), a mapped `ViewUBO` buffer (binding 0) and the rest of the set (`Context::writeViewSet`,
  first time); an acquire semaphore per frame and a present semaphore per swapchain image. With
  `RunArgs::gpu_times` a timestamp query pool (16 queries a frame; warns `gpu_times : the graphics queue has no
  timestamps` when the queue has none). With ray tracing a `SunVisibility` of the swapchain's extent.
- **prepare(frame)** (`Renderer::prepare`): reads back the pass times the frame wrote last time; does nothing
  more and returns false when the `Input` context variable says the window is not showing (minimized), else
  acquires the next swapchain image. `active()` is true when an image was acquired.
- **updateView(frame, time)** (main thread, after the updates): writes the frame's `ViewUBO` from the `Camera`
  context variable: `view_projection` (projection at the swapchain's aspect x view), `position` (camera, w = time),
  `tree_shade` (`treeShadeView`: `RenderSettings::tree_shade`, 1 / 2048, 700 m), the inverse, the previous
  frame's view projection and position (the current ones on the first frame), `lighting` (`lightingView`: the
  desktop tier of `RenderSettings::lighting`: traced range, sun rays, shadowed torches). Starts the
  `SunVisibility` frame, rewrites the view set (`writeViewSet`).
- **record(frame, passes)** (taskflow worker, concurrently with the headset): begins the command buffer, collects
  the profiler's GPU zones, then inside the GPU zone "Desktop":
  - with ray tracing: `SunVisibility::beginScene`, every non-overlay pass that supports the desktop,
    `endScene`, `trace` (zone "Sun rays (desktop)"), then the window's render pass with `composite`
    ("Composite (desktop)");
  - without: the window's render pass (cleared to 0.45, 0.6, 0.8 and depth 1) with the non-overlay passes;
  - then the overlay passes (the UI) in the window's render pass; each pass in its own CPU and GPU zone
    (`record <pass> (desktop)`, `<pass> (desktop)`), a timestamp after each (up to 15).
  - a requested capture copies the swapchain image to a host-visible buffer at the end.
- **present()**: presents with the present semaphore of the image (waits for the monitor with FIFO).
- **requestCapture() / readCapture()**: only when the swapchain is readable and an image was acquired; logs the
  pass times first; `readCapture` returns RGB8 (BGRA swapped).
- `gpu_times`: every 300 frames and at every capture logs `GPU ms over <n> frames (<w> x <h>) : <pass> <ms>, ...
  all <ms>, frame <ms>`.

## Parameters

None of its own. `gpu_times=true` (`RunArgs`) turns the pass timings on.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| RunArgs | context | read | constructor | `gpu_times`: the timestamp query pool |
| Input | context | read | prepare | `window_showing`: no image is acquired and nothing drawn for a minimized window |
| Camera | context | read | updateView | position, view and projection of the desktop view |
| RenderSettings | context | read | updateView (`treeShadeView`, `lightingView`) | `tree_shade` strength and the desktop lighting tier into the `ViewUBO` |

What the passes read while it records is on their pages; `SunVisibility` on [service-SunVisibility](service-SunVisibility.md).

## Side effects

- GPU: command pool, descriptor pool, per-frame mapped uniform buffers, semaphores, query pool, capture buffer;
  records the desktop command buffer every frame the window has an image; acquires and presents swapchain images.
- Log: `gpu_times` warnings and the periodic pass times (info).
- Profiler: CPU and GPU zones per pass, `Acquire the window's image`, `Present`.

## Order and dependencies

- `prepare` after the frame fence is waited for (`Renderer::prepare`); `updateView` after every pass' `update`
  and after the systems wrote the `Camera`; `record` concurrently with [VRRender](service-VRRender.md), so the
  passes it calls must only read shared state.

## Used in scenes
<!-- generated -->

## Tests

- The visual tests (`V0.*`, `V1.1`, `V2.*`, `P1 check`, `P2 check`): every shot is read back through `requestCapture` /
  `readCapture`. `gpu_times` has no test: [`tools/profile_run.py`](../../../tools/profile_run.py) passes it.
