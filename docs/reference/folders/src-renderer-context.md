# `src/renderer/context/`

> [Source](../../../src/renderer/context/) · Parent: [`src/renderer/`](src-renderer.md)

## Summary
The Vulkan and OpenXR context. `Context` aggregates, in construction order, `VRContext` (OpenXR instance first, because it decides Vulkan instance/device extensions, API version and the physical device), `Windu` (SDL2 window and surface), `Instance`, `Device` (+ VMA allocator), `Transfer` (upload batches), `Compute` (ring of compute command buffers) and `Swapchain`, plus the shared `view_layout` (set 0 of every scene pipeline) and the current `frame_index`. The Renderer owns the `Context`; a pointer is stored in the host registry as the `Context*` context variable, which is how systems reach the GPU. The other classes are members, not context variables.

## Files
| File | Contents |
|---|---|
| `context.h` / `context.cpp` | `Context`: member construction, emplaces `Context*`, `VRContext::init`, creates `view_layout`; destructor calls `VRContext::finish` |
| `vr_context.h` / `vr_context.cpp` | `VRContext`: disabled when `Settings::vr_mode == 0` or no runtime / `XR_KHR_vulkan_enable` / headset / stereo view (logged, desktop continues); `vulkanInstanceExtensions`, `vulkanDeviceExtensions`, `vulkanApiVersion`, `vulkanPhysicalDevice`; `init` creates session, stage (or local) reference space, sRGB swapchain format choice, one swapchain per eye with image views; `pose_identity` |
| `windu.h` / `windu.cpp` | `Windu`: SDL video/events init, window from `Settings` (`window_width`/`window_height`, default 1280x720, `fullscreen`), hidden when `RunArgs::window_hidden`; `surface`; `resize()` |
| `instance.h` / `instance.cpp` | `Instance`: SDL + OpenXR instance extensions, validation layer and debug messenger in debug builds, API 1.1 (clamped by OpenXR), surface creation, `supportsPresent` |
| `device.h` / `device.cpp` | `Device`: physical device from OpenXR or best score (discrete > integrated, API >= 1.1), subgroup arithmetic/ballot check (warning), graphics/compute/transfer queue selection (all finally set to the graphics family), logical device, VMA allocator; `getMemoryType`, `isDedicated`; `SET_NAME` debug macro |
| `transfer.h` / `transfer.cpp` | `Transfer`: current upload command buffer, `createImage` (staging copy with layout transitions), `createBuffer` (staged on dedicated GPUs, host-visible otherwise), `flush` (submit with semaphores), `check` (marks completed uploads `ready`), `isEmpty` |
| `compute.h` / `compute.cpp` | `Compute`: `NUM_FRAMES` command buffers with fences, `getCommandBuffer` (marks non-empty), `flush` (submit, advance ring, `prepare` waits the next fence and begins it), `ringIndex`/`ringSize` |
| `swapchain.h` / `swapchain.cpp` | `Swapchain`: `B8G8R8A8_SRGB` preferred, FIFO present (mailbox or immediate with a headset), 3 images when allowed, `TRANSFER_SRC` usage when supported (`readable`), `acquire`, `present`, `findSupportedFormat` |
| `num_frames.h` | `NUM_FRAMES = 3` frames in flight |
| `loader.inl` | `INST_LOAD` / `DEV_LOAD` macros fetching extension function pointers |

## Systems defined here
None.

## Components defined here
- [Context*](../components/Context.md) (context)

## Subfolders
None.
