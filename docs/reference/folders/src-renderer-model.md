# `src/renderer/model/`

> [Source](../../../src/renderer/model/) · Parent: [`src/renderer/`](src-renderer.md)

## Summary
Model loading. `ModelManager` (host context variable, emplaced by the Renderer) loads each glTF 2.0 file once through tinygltf (`loadGLTF`) or builds a mesh from code (`add`), storing the GPU mesh as a `ModelC` on its own entity. Instances are other entities with `Transform` + `ModelRenderable` (defined in `src/view/components/`) pointing at that entity; `ModelPass` draws them. The tinygltf and stb_image implementations are compiled in `gltf_loader.cpp`.

## Files
| File | Contents |
|---|---|
| `model.h` | `ModelVertex` (position, normal, uv), `ModelC` (vertex/index `VmaBuffer`s, `Part`s, `Material`s with base color, image and descriptor set, bounds) |
| `model_manager.h` / `model_manager.cpp` | `ModelManager`: `load(path)`, `get(path)`, `add(key, MeshData)`, path/key -> entity cache |
| `gltf_loader.h` / `gltf_loader.cpp` | `loadGLTF(reg, path)`: accessor decoding (normalized ints), node hierarchy baking, flat normals when missing, winding flip on negative scale, sRGB base color textures, default material, uploads via `Transfer` |

## Systems defined here
- [ModelManager](../systems/ModelManager.md) (service, on demand)

## Components defined here
- [ModelC](../components/ModelC.md)
- [ModelManager](../components/ModelManager.md) (context)

## Subfolders
None.
