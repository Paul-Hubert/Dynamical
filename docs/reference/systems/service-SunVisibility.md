# SunVisibility

`SunVisibility` in [`src/renderer/lighting/sun_visibility.cpp`](../../../src/renderer/lighting/sun_visibility.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (one per render target, owned by `ClassicRender` and `VRRender`, only with ray tracing) · Parameters: no

With ray tracing, what a render target draws its scene in and how the light of the sun reaches the image it
shows ([docs/lighting.md](../../lighting.md), "The sun's visibility"). The scene passes render `colour`
(everything but the directional light; alpha: how far over the surface the ray to the sun starts), `sun`
(what that light adds where nothing hides it) and the depth; `trace()` finds how much of the sun each pixel
sees within the target's `traced_range` and filters it over time; `composite()` writes colour + sun x
visibility into the swapchain (or headset) image. It holds the registry but **reads nothing from it**: the
tier of the target reaches its shaders through the ViewUBO (`lightingView`, written by the target), the
scene through the view set.

## Behaviour

**Constructor** (`ctx, reg, target, extents` of each eye, the target's final render pass):

- The scene render pass: `colour` RGBA16F, `sun` B10G11R11, depth in `Context::sceneDepthFormat()` (with a
  stencil where the GPU has one), all stored and left readable. The visibility pass: one RGBA16F image.
- Nearest and linear clamped samplers, set 1 layout (depth, colour or rays, sun, visibility or history), a
  pool of 5 sets per eye, the layout (set 0 view layout, set 1, push constants `VisibilityPC`: texel size,
  history length).
- Pipelines on `fullscreen.vert.glsl`: `sun_visibility.rt.frag.glsl` (trace), `sun_resolve.frag.glsl`
  (blend with the history), both in the visibility pass; `composite.frag.glsl` in the final pass.
- Per eye: the images and framebuffers (scene, trace, two resolve: this frame's and the one before in turn),
  a depth only view for the shaders, the five sets; the visibility images moved to shader read in the
  transfer command buffer.

**beginFrame()** (main thread, from the target's `updateView`): swaps the visibility images, counts the
history.

**beginScene / endScene** (recording thread of the target): begins the scene pass (clears colour to
(0,0,0,1), sun to 0, depth 1 stencil 0), sets viewport and scissor; ends it. The scene passes record in
between.

**trace(command, eye, view_set)**: the trace pass then the resolve pass into this frame's visibility.

**composite(command, eye, view_set)**: one triangle in the final pass the target has begun; the overlay
passes (UI) come after.

**Destructor**: waits for the device and destroys everything.

## Parameters

None.

## Components and context

None (the registry reference is kept, not used).

## Side effects

- GPU images (colour, sun, depth, rays, two visibility images per eye), render passes, framebuffers, pipelines.
- Records the scene pass begin / end, two fullscreen passes and the composite into the target's command buffer every frame.

## Order and dependencies

- Created by the target before the passes build their pipelines: `scenePass()` is the render pass the scene passes' pipelines are made for (`ScenePass::overlay()` false).
- Traces the top level structure `RayScene` built in the frame's compute commands (view set binding 4), and the probes beyond the traced range.

## Used in scenes
<!-- generated -->

## Tests

- No unit test. Shots by hand: `tests/capture/lighting*.capture.json`.
