# TerrainPass

> **Kind:** pass · **Registry name:** — (scene `renderer.passes` entry `"terrain"`) · **Scope:** n/a (host registry) · **Lane:** per-frame call from `Renderer::render` (`record` on a taskflow worker, once per active render target) · **Needs renderer:** yes
> **Source:** [`src/renderer/passes/terrain_pass.h`](../../../src/renderer/passes/terrain_pass.h), [`src/renderer/passes/terrain_pass.cpp`](../../../src/renderer/passes/terrain_pass.cpp)

## Purpose
Draws the marching cubes chunk meshes produced by `TerrainSys`, one indirect draw per ready chunk, with triplanar texturing (grass layer on top, rock on the sides), simple sun lighting and distance fog (`terrain.vert/frag.glsl`). Also exports `terrainVertexBindings()` / `terrainVertexAttributes()` (the `TerrainVertex` layout: position, normal, uv, 32 bytes), reused by `GrassPass`.

## Lifecycle
- **Construction / params:** `TerrainPass(Context&, entt::registry&)`, created by the Renderer constructor when the scene lists `"terrain"`. Loads `./resources/textures/grass.png` and `./resources/textures/rock.jpg` into a 2-layer `R8G8B8A8_SRGB` array texture through the transfer queue (`loadTextureArray`, throws if missing or differently sized), creates a repeat linear sampler, a material set (set 1) and the pipeline layout (set 0 `view_layout`, set 1 material).
- **createPipeline(target, renderpass):** one graphics pipeline per target from `terrain.vert/frag.glsl.spv`, back-face culling, depth test/write.
- **update:** not overridden.
- **record(command, view):**
  1. Reads `RenderSettings` (`find`) and `TerrainBuffers` (`find`) through a `const entt::registry&`.
  2. Returns if `TerrainBuffers` is absent or `RenderSettings::terrain` is false (draws when `RenderSettings` is absent).
  3. Binds the target's pipeline, set 0 = `view.view_set`, set 1 = material.
  4. For every entity with `ChunkGPU` and `ChunkReady`: binds `vertices[slot]`, `drawIndirect(indirect, slot * indirect_stride, 1, 0)`.
- **finish:** none; the destructor destroys pipelines, layout, sampler, view, layout and pool (the texture is a `shared_ptr`).

## Expected behavior
- No draws without the `Terrain` system (no `TerrainBuffers`) or with `RenderSettings::terrain == false`.
- Every chunk with both `ChunkGPU` and `ChunkReady` is drawn on every active target; there is no frustum or distance culling.
- Identical output for desktop and each VR eye except for the view set.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`RenderSettings`](../components/RenderSettings.md) | host | read | record | `terrain` toggle |
| [`TerrainBuffers`](../components/TerrainBuffers.md) | host | read | record | `vertices`, `indirect`, `indirect_stride` |
| [`ChunkGPU`](../components/ChunkGPU.md) | host | read | record | `slot` |
| [`ChunkReady`](../components/ChunkReady.md) | host | read | record | View filter |

## Side effects
- Construction: reads two texture files, records an image upload in the transfer command buffer, creates Vulkan objects.
- `record`: records bind and `drawIndirect` commands into the target's command buffer.

## Ordering and dependencies
- `record` runs concurrently for the desktop and VR targets on taskflow workers; it only reads registry state (const registry, storages pre-created by `TerrainSys::init`).
- Must be listed before `grass` in `renderer.passes` so grass shells are drawn over the opaque terrain (draw order = list order).
- Depends on `TerrainSys` having run earlier in the frame.

## Related
[TerrainSys](TerrainSys.md), [GrassPass](GrassPass.md), [Renderer](Renderer.md), [`src/renderer/passes/`](../folders/src-renderer-passes.md).
