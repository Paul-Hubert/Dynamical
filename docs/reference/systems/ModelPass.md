# ModelPass

> **Kind:** pass · **Registry name:** — (scene `renderer.passes` entry `"model"`) · **Scope:** n/a (host registry) · **Lane:** per-frame call from `Renderer::render` (`update` on the main thread, `record` on a taskflow worker per active target) · **Needs renderer:** yes
> **Source:** [`src/renderer/passes/model_pass.h`](../../../src/renderer/passes/model_pass.h), [`src/renderer/passes/model_pass.cpp`](../../../src/renderer/passes/model_pass.cpp)

## Purpose
Draws every entity that has a `Transform` and a `ModelRenderable`, using the `ModelC` stored on the entity `ModelRenderable::model` points to (glTF meshes loaded by `ModelManager`). Lit with a fixed sun, base color texture times material color times instance tint, distance fog (`model.vert/frag.glsl`).

## Lifecycle
- **Construction / params:** `ModelPass(Context&, entt::registry&)`, created by the Renderer when the scene lists `"model"`. Creates the `ModelC`, `ModelRenderable` and `Transform` storages (so worker threads never create them), a descriptor pool of `max_materials = 1024` sets (free-able), material layout (set 1: one combined image sampler), repeat linear sampler, a 1x1 white texture, and the layout (set 0 `view_layout`, set 1 material, push constants `ModelPC { mat4 model; vec4 color; }` vertex + fragment).
- **createPipeline:** `model.vert/frag.glsl.spv`, `ModelVertex` layout (position, normal, uv), default state.
- **update(frame_index):** main thread, before recording. For every `ModelC`, every material with a null `set` gets one: an image view of `material.image` (or the white texture when null), cached per `VmaImage*`, and a newly allocated descriptor set.
- **record(command, view):**
  1. `find<RenderSettings>()`; returns if present and `models` is false.
  2. Binds pipeline and set 0.
  3. For every `Transform` + `ModelRenderable` entity: `try_get<ModelC>(renderable.model)`; skips if absent or without vertex/index buffers. Binds buffers, computes `transform.matrix()`.
  4. Per part: skips if the material has no set; pushes `{matrix, material.base_color * renderable.tint}`, binds set 1, `drawIndexed(index_count, 1, first_index, 0, 0)`.
- **finish:** none; destructor destroys pipelines, cached image views, layout, sampler, layout, pool.

## Expected behavior
- An instance whose `model` entity has no `ModelC` (e.g. `entt::null` after a failed load, or after `Renderer::finish`) is silently skipped.
- A model loaded this frame is drawn this frame: `update` creates its descriptor sets before recording. Its buffers may still be uploading on a dedicated GPU; the transfer submit is ordered before the graphics submit by a semaphore.
- Descriptor sets are never freed while the pass lives; more than 1024 materials in total exhausts the pool (allocation failure, not handled).
- Image views are shared by every material using the same `VmaImage`.
- No culling; one draw per part per instance per target.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`ModelC`](../components/ModelC.md) | host | write | construction | Storage created |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | write | construction | Storage created |
| [`Transform`](../components/Transform.md) | host | write | construction | Storage created |
| [`ModelC`](../components/ModelC.md) | host | readwrite | update | `materials[].set` filled from `materials[].image` |
| [`RenderSettings`](../components/RenderSettings.md) | host | read | record | `models` toggle |
| [`Transform`](../components/Transform.md) | host | read | record | `matrix()` |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | read | record | `model`, `tint` |
| [`ModelC`](../components/ModelC.md) | host | read | record | Buffers, parts, materials |

## Side effects
- Construction: uploads the white texture on the transfer queue.
- `update`: allocates descriptor sets and creates image views (Vulkan calls on the main thread).
- `record`: records draw commands.

## Ordering and dependencies
- `update` must run before the parallel `record` (guaranteed by `Renderer::render`); it is the only writer.
- `record` runs concurrently on both targets and only reads the registry (const view).
- Entities whose `Transform` is interpolated by physics are drawn at the value left by the systems of this frame.

## Related
[ModelManager](ModelManager.md), [Renderer](Renderer.md), [`src/renderer/model/`](../folders/src-renderer-model.md), [`src/renderer/passes/`](../folders/src-renderer-passes.md).
