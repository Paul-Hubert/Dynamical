# ui

`UIPass` in [`src/renderer/passes/ui_pass.cpp`](../../../src/renderer/passes/ui_pass.cpp) · Scope: Host · Needs renderer: yes
· Lanes: `renderer.passes` (`"ui"`) · Parameters: no

Dear ImGui on the desktop window only (`supports()` is false for the headset). An overlay pass
(`overlay()` true): it draws in the render pass of the swapchain image after the scene, over the composite
of `SunVisibility` with ray tracing. It touches no component or context variable: what the windows show is
built by the systems that call ImGui (`UISys` opens the frame with `ImGui::NewFrame()`, the debug menu and
others add windows).

## Behaviour

**Constructor**: creates the ImGui context when there is none (dark style), adds the default font, turns
the ini file off, uploads the font atlas (RGBA8), a clamped linear sampler, the set (binding 0 the atlas),
the layout (that set only, 16 bytes of vertex push constants: scale and translation).

**createPipeline(Desktop, renderpass)** (desktop only, with the final render pass of `ClassicRender`):
`uirender.vert.glsl.spv` + `uirender.frag.glsl.spv`, `ImDrawVert` input, no culling, no depth, alpha
blending.

**update(frame_index)** (main thread): `ImGui::Render()`, keeps the draw data (none when empty); grows the
mapped vertex and index buffers of this frame in flight (by half again over the need) and copies every
draw list into them.

**record(command, view)** (desktop thread only): nothing without draw data or with a zero sized display;
binds the pipeline, the font set, the buffers of `view.frame_index`; sets the viewport and the projection
push constants; for every command of every list (user callbacks skipped) binds the texture set its
`TextureId` points to (a `vk::DescriptorSet*`), sets the clipped scissor and draws indexed, then restores the
font set. Ends by restoring the full scissor.

**Destructor**: pipelines, layout, set layout, pool, sampler, view. The ImGui context is left.

## Parameters

None. `hide_ui` in a capture script hides it by other means (not this pass).

## Components and context

None.

## Side effects

- Creates the global ImGui context if needed and uploads its font atlas.
- Calls `ImGui::Render()` once a frame (ends the ImGui frame `UISys` began).
- Writes the ImGui vertices and indices into mapped buffers per frame in flight; records the UI draws into the desktop command buffer.

## Order and dependencies

- `UISys` (`"UI"`, host pre lane) must call `ImGui::NewFrame()` every frame before the renderer runs: `ImGui::Render()` here closes that frame.
- Drawn last on the desktop, after the scene (or the composite), in the final render pass.

## Used in scenes
<!-- generated -->

## Tests

- Visual: `P1 check` (`base_interactive.capture.json`), `P2 check` (`worlds_stepper.capture.json`: scripted clicks on the WorldStepper window).
