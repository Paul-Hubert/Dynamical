# src/renderer/classic_render

The desktop render target ([ClassicRender](../systems/service-ClassicRender.md)): the swapchain's render pass,
framebuffers and depth buffers, a command buffer and a view UBO per frame, the frame capture of the shots and the
per-pass GPU timings (`gpu_times=true`).

## Files

| File | What |
|---|---|
| [`classic_render.h`](../../../src/renderer/classic_render/classic_render.h) / [`classic_render.cpp`](../../../src/renderer/classic_render/classic_render.cpp) | `ClassicRender`: `prepare` (acquire, unless the window is minimized), `updateView` (the `ViewUBO` from the `Camera`), `record` (scene passes, with ray tracing through `SunVisibility`, then overlays), `present`, `requestCapture` / `readCapture`, pass times |
| [`renderpass.h`](../../../src/renderer/classic_render/renderpass.h) / [`renderpass.cpp`](../../../src/renderer/classic_render/renderpass.cpp) | `ClRenderpass`: the render pass of the swapchain images (colour + depth / stencil of `Context::sceneDepthFormat`), a framebuffer and depth image per swapchain image |

## Systems

None registered here. [ClassicRender](../systems/service-ClassicRender.md) touches the registry.

## Components and context variables

None defined here.

## Dependencies

- `src/renderer/context`, `src/renderer/lighting` (`SunVisibility`, `lightingView`), `src/renderer/tree_shade.h`
  (`treeShadeView`), `src/logic/components/camera.h`, `src/view/components/input.h`, `src/scene/run_args.h`.

## Design docs

[docs/lighting.md](../../lighting.md) (the sun's visibility and the composite), [docs/profiling.md](../../profiling.md) (`gpu_times`, zones).
