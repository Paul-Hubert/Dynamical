# UIPass

> **Kind:** pass · **Registry name:** — (scene `renderer.passes` entry `"ui"`) · **Scope:** n/a · **Lane:** per-frame call from `Renderer::render` (`update` on the main thread, `record` on a taskflow worker, desktop target only) · **Needs renderer:** yes
> **Source:** [`src/renderer/passes/ui_pass.h`](../../../src/renderer/passes/ui_pass.h), [`src/renderer/passes/ui_pass.cpp`](../../../src/renderer/passes/ui_pass.cpp)

## Purpose
Renders Dear ImGui on the desktop window. Finalizes the frame's ImGui draw data (`ImGui::Render`), copies vertices and indices into per-frame mapped buffers, and records clipped indexed draws. It stores the registry reference but reads no component or context variable.

## Lifecycle
- **Construction / params:** `UIPass(Context&, entt::registry&)`, created by the Renderer when the scene lists `"ui"`. Creates the ImGui context if none exists (dark style), adds the default font, disables the ini file, uploads the RGBA32 font atlas (`R8G8B8A8_UNORM`) through the transfer queue, clamp sampler, descriptor set, layout with 16 bytes of vertex push constants (scale, translate). `NUM_FRAMES` vertex/index buffers, created lazily.
- **supports(target):** `Desktop` only; the Renderer never builds a VR pipeline for it and `VRRender::record` skips it.
- **createPipeline:** `uirender.vert/frag.glsl.spv`, `ImDrawVert` layout, no culling, no depth test/write, alpha blending.
- **update(frame_index):** `ImGui::Render()`; if there is no draw data or 0 vertices, clears `draw_data` and returns. Otherwise grows the frame's buffers to 1.5x the needed size when too small (CPU-to-GPU, mapped) and copies every draw list.
- **record(command, view):** returns if no draw data or a non-positive framebuffer size. Binds pipeline, font set, buffers (16- or 32-bit indices from `sizeof(ImDrawIdx)`), sets the viewport to the ImGui framebuffer size, pushes scale/translate. Per draw command (user callbacks skipped): binds `*(vk::DescriptorSet*) TextureId` when non-null, computes the clip rectangle, sets the scissor and draws if the rectangle is non-empty and on screen, then rebinds the font set. Finally restores the full scissor.
- **finish:** none; destructor destroys Vulkan objects (the ImGui context is not destroyed here).

## Expected behavior
- `update` calls `ImGui::Render()` every frame, even when the desktop target is inactive (window hidden): ImGui frames must be begun every frame by `UISys` (rendered scenes must include `common/desktop.json`).
- Nothing is drawn on VR targets.
- Leaves the viewport set to the ImGui framebuffer size and the scissor reset to the full extent.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| None | | | | Uses ImGui global state only |

## Side effects
- ImGui global state: may create the ImGui context, builds the font atlas, calls `ImGui::Render()` each frame.
- Uploads the font atlas on the transfer queue; allocates/grows mapped buffers.
- Records draw commands.

## Ordering and dependencies
- Must be last in `renderer.passes` to overlay the scene (no depth test).
- `UISys` (which calls `ImGui::NewFrame`) and the ImGui-building systems (debug menu, etc.) must tick before `Renderer::render`. `UISys::init` relies on this pass usually having created the ImGui context first (the Renderer is constructed before the systems).
- `update` on the main thread; `record` on one worker (desktop task).

## Related
[Renderer](Renderer.md), [ClassicRender](ClassicRender.md), [`src/renderer/passes/`](../folders/src-renderer-passes.md).
