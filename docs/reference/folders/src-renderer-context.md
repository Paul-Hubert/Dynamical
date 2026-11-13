# src/renderer/context

The Vulkan and OpenXR context of the renderer ([Context](../systems/service-Context.md)): OpenXR first (it picks
the Vulkan extensions and the GPU), then the SDL window, the Vulkan instance, the device with its queues, VMA and
ray tracing support, the GPU profiler, the transfer and compute command streams and the swapchain; plus the
layout of the view set (set 0 of every scene pipeline) and the handles its bindings are written from.

## Files

| File | What |
|---|---|
| [`context.h`](../../../src/renderer/context/context.h) / [`context.cpp`](../../../src/renderer/context/context.cpp) | `Context`: the members in construction order, `Context*` in the registry context, the view set layout (bindings 0 to 12), `writeViewSet`, `viewPoolSizes`, `sceneDepthFormat`, `sceneAttachments`, `rayInputUsage` |
| [`vr_context.h`](../../../src/renderer/context/vr_context.h) / [`vr_context.cpp`](../../../src/renderer/context/vr_context.cpp) | `VRContext`: OpenXR instance, system, session, reference space (stage or local), one swapchain per eye; disabled with a logged reason when there is no runtime or headset (`Settings::vr_mode`) |
| [`windu.h`](../../../src/renderer/context/windu.h) / [`windu.cpp`](../../../src/renderer/context/windu.cpp) | `Windu`: SDL2 window (size, fullscreen, hidden for captures) and its Vulkan surface |
| [`instance.h`](../../../src/renderer/context/instance.h) / [`instance.cpp`](../../../src/renderer/context/instance.cpp) | `Instance`: Vulkan 1.2 (or 1.1) instance with SDL's and OpenXR's extensions, validation in debug builds |
| [`device.h`](../../../src/renderer/context/device.h) / [`device.cpp`](../../../src/renderer/context/device.cpp) | `Device`: physical device (OpenXR's or the best scored), queues (`graphics`, `compute`, `transfer`, indices `g_i` ...), VMA allocator, `RayContext`; debug names |
| [`ray_context.h`](../../../src/renderer/context/ray_context.h) / [`ray_context.cpp`](../../../src/renderer/context/ray_context.cpp) | `RayContext`: detects acceleration structures and ray queries (`enabled()`, `ray_tracing=false` forces off), their extensions and feature chain |
| [`gpu_profile.h`](../../../src/renderer/context/gpu_profile.h) / [`gpu_profile.cpp`](../../../src/renderer/context/gpu_profile.cpp) | `GpuProfile`: the Tracy Vulkan context (calibrated timestamps where available), `DY_GPU_ZONE`, `collect()`; nothing without `profile=true` |
| [`transfer.h`](../../../src/renderer/context/transfer.h) / [`transfer.cpp`](../../../src/renderer/context/transfer.cpp) | `Transfer`: staging uploads (`createImage`, `createImageMips`, `createBuffer`, `updateImage`), flushed once a frame, `check()` releases finished staging |
| [`compute.h`](../../../src/renderer/context/compute.h) / [`compute.cpp`](../../../src/renderer/context/compute.cpp) | `Compute`: the ring of compute command buffers systems and the lighting record into (`getCommandBuffer`, `ringIndex`, `flush`, `isEmpty`) |
| [`swapchain.h`](../../../src/renderer/context/swapchain.h) / [`swapchain.cpp`](../../../src/renderer/context/swapchain.cpp) | `Swapchain`: surface format (sRGB BGRA8), present mode (FIFO, mailbox / immediate with VR or `gpu_times`), images, `acquire`, `present`, `findSupportedFormat` |
| [`num_frames.h`](../../../src/renderer/context/num_frames.h) | `NUM_FRAMES = 3`, frames in flight |
| [`loader.inl`](../../../src/renderer/context/loader.inl) | `INST_LOAD` / `DEV_LOAD` macros: load a Vulkan function pointer from the instance or the device |

## Systems

None registered here. [Context](../systems/service-Context.md) touches the registry.

## Components and context variables

- [Context](../components/Context.md) (context variable, held as `Context*`).

## Worth knowing

- `reg.ctx().get<Context*>()` is how code that only has the registry reaches the GPU (the `Terrain` system,
  `ModelManager`, `loadGLTF`, the `Input` system).
- `Context::writeViewSet(set, frame, first)`: every binding of a view set but the `ViewUBO`, from
  `tree_shade_view`, `soil_view`, `lights_buffers`, `tlas`, `ray_geometries`, `ray_textures`, the probe pages and
  the plant volumes, which their owners keep up to date.
- `ctx.device.ray.enabled()`: the one question about ray tracing.

## Dependencies

- `src/logic/settings` (`Settings`), `src/scene` (`RunArgs`), `src/renderer/util` (vk, VMA wrappers, XR helpers),
  `src/renderer/lighting/lights.h` (`LightsUBO` size), `src/util/profile.h`; SDL2, Vulkan, OpenXR, VMA, Tracy.

## Design docs

[docs/lighting.md](../../lighting.md) (ray tracing, view set), [docs/profiling.md](../../profiling.md) (GPU zones),
[docs/port-mc-grass-gltf-vr-jolt.md](../../port-mc-grass-gltf-vr-jolt.md) (VR).
