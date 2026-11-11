# `src/renderer/`

> [Source](../../../src/renderer/) · Parent: [`src/`](src.md)

## Summary
The Vulkan renderer. `Renderer` owns the device context (`context/`), two render targets recorded in parallel every frame (the desktop window in `classic_render/`, the OpenXR headset in `vr_render/`) and the scene passes (`passes/`) chosen by the scene's `renderer.passes` list. Frames are paced by `NUM_FRAMES = 3` fences; transfer, compute (marching cubes recorded by `TerrainSys` in `terrain/`) and graphics are submitted in that order, chained with semaphores, with a single graphics submission holding both targets' command buffers. `model/` loads glTF files into `ModelC` entities, `util/` wraps VMA, pipelines, textures and OpenXR helpers, and `shaders/` holds the GLSL compiled to SPIR-V by CMake. Everything here lives in the host registry and exists only in rendered runs (`dynamical`, not `dynamical_train`).

## Files
| File | Contents |
|---|---|
| `renderer.h` / `renderer.cpp` | `Renderer`: construction of context, targets and passes; `preinit`/`init`/`prepare`/`render`/`finish`; frame sync (fence + transfer/compute semaphores per frame); `requestCapture`/`takeCapture` |
| `scene_pass.h` | `RenderTarget` (Desktop, VR), `RenderView` (target, frame index, eye, view set, extent), abstract `ScenePass` (`createPipeline`, `update` on the main thread, `record` concurrent and read-only, `supports`) |
| `view_ubo.h` | `ViewUBO { mat4 view_projection; vec4 position /* xyz camera, w time */ }`, set 0 binding 0 of every scene pipeline |

## Systems defined here
- [Renderer](../systems/Renderer.md) (service)

## Components defined here
None directly (see subfolders).

## Subfolders
- [`classic_render/`](src-renderer-classic_render.md)
- [`context/`](src-renderer-context.md)
- [`model/`](src-renderer-model.md)
- [`passes/`](src-renderer-passes.md)
- [`shaders/`](src-renderer-shaders.md)
- [`terrain/`](src-renderer-terrain.md)
- [`util/`](src-renderer-util.md)
- [`vr_render/`](src-renderer-vr_render.md)
