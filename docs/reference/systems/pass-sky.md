# sky

`SkyPass` in [`src/renderer/passes/sky_pass.cpp`](../../../src/renderer/passes/sky_pass.cpp) · Scope: Host · Needs renderer: yes
· Lanes: always, first (scenes do not list it) · Parameters: no

The sky behind everything: `skyColor` of the shaders, the discs of the sun and of the moon, stars at night
(`sky.vert.glsl`, `sky.frag.glsl`, [docs/lighting.md](../../lighting.md)). The `Renderer` creates it before
the passes the scene lists, so it is drawn first on every target. What it draws comes from the view set
alone: the `LightsUBO` of the frame (binding 3, filled by `Lights` from the `Sky` context variable) and the
`ViewUBO`. It touches no component or context variable.

## Behaviour

**Constructor**: a pipeline layout with set 0 only (`Context::view_layout`), no push constants.

**createPipeline(target, renderpass)**: `sky.vert.glsl.spv` + `sceneFragmentShader(ctx, "sky")` (`sky.frag.glsl.rt.spv`
under ray tracing, for the two colour attachments of the scene pass of `SunVisibility`), `Context::sceneAttachments()`, no
vertex input, no culling, no depth test or write.

**update**: none (the default).

**record(command, view)**: binds the pipeline and `view.view_set`, draws one triangle over the screen.

**Destructor**: pipelines and layout.

## Parameters

None.

## Components and context

None. The sun, the moon and the sky colours it draws are those `Lights::update` wrote into the frame's
lights buffer from `Sky` (see [service-Lights](service-Lights.md)).

## Side effects

- One fullscreen triangle per target and eye.

## Order and dependencies

- First of the scene passes (index 0 of `Renderer::passes`); every other pass draws over it with depth.
- `Lights::update` runs at the start of `Renderer::render`, before the recording.

## Used in scenes
<!-- generated -->

## Tests

- `LIGHT.1` to `LIGHT.4` in [`tests/unit/lighting.cpp`](../../../tests/unit/lighting.cpp) (the sun, the moon and the sky colours, mirrored by the shader). Visual: every capture (`V0.*`, `V1.1`, `V2.*`).
