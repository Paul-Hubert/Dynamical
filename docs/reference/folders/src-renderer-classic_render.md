# `src/renderer/classic_render/`

> [Source](../../../src/renderer/classic_render/) · Parent: [`src/renderer/`](src-renderer.md)

## Summary
The desktop window render target. `ClassicRender` acquires a swapchain image (skipped while the window is hidden), writes the desktop `ViewUBO` from the `Camera` context variable, records every pass supporting `RenderTarget::Desktop` into its per-frame command buffer, presents, and can read the frame back for captures. `ClRenderpass` holds the render pass and per-swapchain-image depth buffers and framebuffers.

## Files
| File | Contents |
|---|---|
| `classic_render.h` / `classic_render.cpp` | `ClassicRender`: per-frame command buffer, UBO, descriptor set, acquire semaphore; per-image present semaphores; `prepare`, `updateView`, `record`, `present`, `requestCapture`/`recordCapture`/`readCapture` |
| `renderpass.h` / `renderpass.cpp` | `ClRenderpass`: color (swapchain format, final `PRESENT_SRC`) + depth (`D32_SFLOAT` or `D16_UNORM`) render pass, one depth image/view and framebuffer per swapchain image |

## Systems defined here
- [ClassicRender](../systems/ClassicRender.md) (service)

## Components defined here
None.

## Subfolders
None.
