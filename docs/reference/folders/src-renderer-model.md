# src/renderer/model

glTF 2.0 models: the loader (tinygltf, flattened into one vertex and index buffer with the node transforms baked
in), the `ModelC` component that holds a loaded mesh on its own entity, and the
[ModelManager](../systems/service-ModelManager.md) context variable that caches them by path. Instances are
entities with `Transform` + `ModelRenderable` (`src/view/components/model_renderable.h`) drawn by the
[model pass](../systems/pass-model.md).

## Files

| File | What |
|---|---|
| [`model.h`](../../../src/renderer/model/model.h) | `ModelVertex` (position, normal, uv) and the `ModelC` component (buffers, parts, materials, bounds, ray mesh index) |
| [`gltf_loader.h`](../../../src/renderer/model/gltf_loader.h) / [`gltf_loader.cpp`](../../../src/renderer/model/gltf_loader.cpp) | `loadGLTF(reg, path)`: `.gltf` / `.glb`, triangles only, POSITION, NORMAL, TEXCOORD_0, base colour factor and RGBA8 texture; throws on malformed files |
| [`model_manager.h`](../../../src/renderer/model/model_manager.h) / [`model_manager.cpp`](../../../src/renderer/model/model_manager.cpp) | `ModelManager`: `load(path)`, `get(path)`, `add(key, MeshData)` for meshes built in code |

## Systems

None registered here. [ModelManager](../systems/service-ModelManager.md) touches the registry.

## Components and context variables

- [ModelC](../components/ModelC.md) (component, on the model's own entity).
- [ModelManager](../components/ModelManager.md) (context variable).

## Dependencies

- `src/renderer/context` (`Context*`, `Transfer`), `src/renderer/util/vmapp.h`, `src/util/primitive_mesh.h`
  (`MeshData`), `src/util/log.h`; tinygltf.

## Design docs

[docs/port-mc-grass-gltf-vr-jolt.md](../../port-mc-grass-gltf-vr-jolt.md).
