# `src/renderer/vr_render/`

> [Source](../../../src/renderer/vr_render/) · Parent: [`src/renderer/`](src-renderer.md)

## Summary
The OpenXR headset render target and controller input. `VRRender` runs the OpenXR frame loop, writes one `ViewUBO` per eye from the located views and `VRRig`, and records the passes supporting `RenderTarget::VR` once per eye. `VRActions` creates the OpenXR action set and fills the `VRInput` context variable each VR frame. Both context variables (`VRRig`, `VRInput`, defined in `src/logic/components/vr.h`) are emplaced here even when VR is disabled.

## Files
| File | Contents |
|---|---|
| `vr_render.h` / `vr_render.cpp` | `VRRender`: session event polling, `xrWaitFrame`/`xrBeginFrame`/`xrEndFrame`, eye image acquire/release, `projection_fov` (asymmetric FOV to Vulkan clip space, near 0.05, far 3000), `pose_matrix`, per-eye recording |
| `vr_input.h` / `vr_input.cpp` | `VRActions`: action set `gameplay` (hand pose, trigger, grip, thumbstick, primary), bindings for Oculus Touch, Valve Index and KHR simple controllers, hand/view spaces, `sync(XrTime)` writing `VRInput` |
| `renderpass.h` / `renderpass.cpp` | `VRRenderpass`: render pass in the OpenXR swapchain format (final `COLOR_ATTACHMENT_OPTIMAL`), depth images and framebuffers per eye and swapchain image; empty when VR is disabled |

## Systems defined here
- [VRRender](../systems/VRRender.md) (service)
- [VRActions](../systems/VRActions.md) (service)

## Components defined here
None (`VRRig` and `VRInput` are defined in `src/logic/components/vr.h`).

## Subfolders
None.
