# ModelManager

> **Kind:** service · **Registry name:** — · **Scope:** n/a (host registry) · **Lane:** on demand (called by systems and by the `ModelRenderable` scene component loader) · **Needs renderer:** yes
> **Source:** [`src/renderer/model/model_manager.h`](../../../src/renderer/model/model_manager.h), [`src/renderer/model/model_manager.cpp`](../../../src/renderer/model/model_manager.cpp), [`src/renderer/model/gltf_loader.h`](../../../src/renderer/model/gltf_loader.h), [`src/renderer/model/gltf_loader.cpp`](../../../src/renderer/model/gltf_loader.cpp)

## Purpose
Context variable that loads each model once and hands out the entity holding its `ModelC`. Instances are separate entities with a `ModelRenderable` pointing to that entity. Not ticked: its methods are called by scene setup and systems (`ModelRenderable` scene component, `spawn.cpp`, `SphereViewSys`, `WorldViewSys`, base scene systems).

## Lifecycle
- **Construction / params:** `ModelManager(entt::registry&)`, emplaced by the Renderer constructor. No params.
- **load(path):** returns the cached entity for `path` if any. Otherwise creates an entity, emplaces `ModelC` from `loadGLTF(reg, path)`, caches and returns it. On any exception logs `Could not load model <path> : <what>` and returns `entt::null` (the entity created just before is not destroyed and stays empty; the path is not cached, so a later call retries).
- **loadGLTF(reg, path):** `.glb` binary or ASCII glTF via tinygltf (warnings logged, failure throws). Materials: base color factor and base color texture (only 8-bit RGBA images, uploaded once each as `R8G8B8A8_SRGB`; others logged and left untextured); a default material is appended last. Walks the default scene's node tree (or all meshes when there is no scene), baking node matrices (matrix or T * R * S) into positions and the inverse-transpose into normals. Triangle primitives only (others logged and skipped); primitives without `POSITION` skipped; missing normals -> flat normals; missing UVs -> 0; missing indices -> sequential; negative determinant flips winding. Throws `no triangles` if empty. Computes bounds, uploads vertex and index buffers with `Transfer::createBuffer`, logs vertex/triangle counts.
- **add(key, MeshData):** cached by `key`; builds `ModelVertex`s from positions/normals/uvs, bounds, one white material and one part, uploads, creates the entity.
- **get(path):** cache lookup only, `entt::null` if absent.
- **finish:** erased by `Renderer::finish` (after `reg.clear<ModelC>()`).

## Expected behavior
- Two `load` calls with the same string return the same entity; different spellings of one file load it twice.
- The returned model is drawable in the same frame (ModelPass creates descriptor sets in `update`; uploads are submitted before graphics).
- Throws nothing: load errors become `entt::null` and a log line.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Context*`](../components/Context.md) | host | read | load / add | Transfer and device for uploads |
| [`ModelC`](../components/ModelC.md) | host | write +create | load / add | One new entity per distinct path/key |

## Side effects
- Reads glTF/GLB files and their external buffers/images from disk.
- Records buffer/image uploads into the transfer command buffer (or host-visible buffers on non-dedicated GPUs).
- Creates entities. Logs info, warnings and errors.

## Ordering and dependencies
- Only valid after the Renderer exists; must not be called from the parallel recording phase (main thread only).
- Uploads made during `init` are flushed by `Renderer::init`; later ones by the next `Renderer::render`.

## Related
[ModelPass](ModelPass.md), [Renderer](Renderer.md), [`src/renderer/model/`](../folders/src-renderer-model.md), `ModelRenderable` in `src/view/components/model_renderable.h`.
