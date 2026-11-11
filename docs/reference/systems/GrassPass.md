# GrassPass

> **Kind:** pass · **Registry name:** — (scene `renderer.passes` entry `"grass"`) · **Scope:** n/a (host registry) · **Lane:** per-frame call from `Renderer::render` (`record` on a taskflow worker, once per active render target) · **Needs renderer:** yes
> **Source:** [`src/renderer/passes/grass_pass.h`](../../../src/renderer/passes/grass_pass.h), [`src/renderer/passes/grass_pass.cpp`](../../../src/renderer/passes/grass_pass.cpp), [`src/renderer/passes/raycast_data.h`](../../../src/renderer/passes/raycast_data.h)

## Purpose
Raymarched grass: redraws the terrain chunk meshes as a shell lifted by the grass height; the fragment shader marches the view ray through a few layers using a precomputed blade distance/normal lookup (`resources/raycast_data.bin`, baked by `tools/raycast`) and a tileable noise texture, discarding on slopes (normal.y < 0.75) so the rock of the terrain pass stays visible.

## Lifecycle
- **Construction / params:** `GrassPass(Context&, entt::registry&)`, created by the Renderer when the scene lists `"grass"`.
  1. Descriptor pool/layout with two combined image samplers (set 1), repeat linear sampler.
  2. Reads `./resources/raycast_data.bin` with a cereal portable binary archive into `RaycastData` (32 angles x 128 x 128 `u8vec4` samples). If missing, logs an error (`grass will look flat`) and uploads zeros. Uploads it as a 3D `R8G8B8A8_UNORM` image 128 x 128 x 32.
  3. Generates a 512 x 512 `R8G8B8A8_SNORM` tileable noise (`TiledNoise::octaveNoise`, 3 channels) on the CPU and uploads it.
  4. Pipeline layout: set 0 `view_layout`, set 1 the two textures, push constants `GrassPC { vec4 base_normal; float tile_size, grass_height, noise_frequency, noise_amplitude; }` for vertex + fragment.
- **createPipeline:** `grass.vert/frag.glsl.spv`, terrain vertex layout, default state (back cull, depth test/write, no blending).
- **update:** not overridden.
- **record(command, view):**
  1. `find<RenderSettings>()` and `find<TerrainBuffers>()` on a const registry; returns if `TerrainBuffers` is absent, `RenderSettings` is absent, or `RenderSettings::grass` is false.
  2. Push constants from `RenderSettings::grass_params` (`base_normal`, `tile_size`, `height`, `noise_frequency`, `noise_amplitude`).
  3. Binds pipeline, sets, pushes constants; one `drawIndirect` per `ChunkGPU` + `ChunkReady` entity, as `TerrainPass`.
- **finish:** none; destructor destroys Vulkan objects.

## Expected behavior
- Unlike `TerrainPass`, nothing is drawn when the `RenderSettings` context variable is absent.
- Draws the same chunk set as `TerrainPass` with the same indirect commands.
- Changing `RenderSettings::grass_params` (debug menu) takes effect on the next recorded frame.
- A missing `raycast_data.bin` is not fatal.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`RenderSettings`](../components/RenderSettings.md) | host | read | record | `grass`, `grass_params` |
| [`TerrainBuffers`](../components/TerrainBuffers.md) | host | read | record | Vertex and indirect buffers |
| [`ChunkGPU`](../components/ChunkGPU.md) | host | read | record | `slot` |
| [`ChunkReady`](../components/ChunkReady.md) | host | read | record | View filter |

## Side effects
- Construction: reads `./resources/raycast_data.bin` (logs an error if missing), CPU noise generation (512² x 3 octave noise evaluations), two image uploads on the transfer queue.
- `record`: records draw commands.

## Ordering and dependencies
- Must follow `terrain` in `renderer.passes` (draws over the opaque terrain with depth test).
- `record` runs concurrently on both targets; read-only.
- Depends on `TerrainSys` for meshes.

## Related
[TerrainPass](TerrainPass.md), [TerrainSys](TerrainSys.md), [Renderer](Renderer.md), [`src/renderer/shaders/`](../folders/src-renderer-shaders.md) (`grass.vert/frag.glsl`).
