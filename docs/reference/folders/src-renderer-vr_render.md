# src/renderer/vr_render

The headset render target ([VRRender](../systems/service-VRRender.md)), drawn alongside the desktop every frame,
and the OpenXR actions of the controllers ([VRActions](../systems/service-VRActions.md)) that fill the `VRInput`
context variable.

## Files

| File | What |
|---|---|
| [`vr_render.h`](../../../src/renderer/vr_render/vr_render.h) / [`vr_render.cpp`](../../../src/renderer/vr_render/vr_render.cpp) | `VRRender`: session events, `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame`, eye images, per-eye `ViewUBO` from the located views and the `VRRig`, recording per eye (with ray tracing through `SunVisibility`) |
| [`vr_input.h`](../../../src/renderer/vr_render/vr_input.h) / [`vr_input.cpp`](../../../src/renderer/vr_render/vr_input.cpp) | `VRActions`: action set, bindings for Touch, Index and the simple controller, `sync()` writes head and hands into `VRInput` |
| [`renderpass.h`](../../../src/renderer/vr_render/renderpass.h) / [`renderpass.cpp`](../../../src/renderer/vr_render/renderpass.cpp) | `VRRenderpass`: the render pass of the eye swapchains, a framebuffer and depth buffer per eye and swapchain image |

## Systems

None registered here. [VRRender](../systems/service-VRRender.md) and [VRActions](../systems/service-VRActions.md)
touch the registry (`VRRig`, `VRInput`, both defined in `src/logic/components/vr.h`).

## Components and context variables

None defined here.

## Dependencies

- `src/renderer/context` (`VRContext`), `src/renderer/util/xr.h`, `src/renderer/lighting` (`SunVisibility`,
  `lightingView`), `src/logic/components/vr.h`; OpenXR.

## Design docs

[docs/port-mc-grass-gltf-vr-jolt.md](../../port-mc-grass-gltf-vr-jolt.md), [docs/players.md](../../players.md)
(what reads `VRInput`). Not run in a headset yet.
