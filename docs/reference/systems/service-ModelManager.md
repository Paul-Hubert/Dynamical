# ModelManager

`ModelManager` in [`src/renderer/model/model_manager.cpp`](../../../src/renderer/model/model_manager.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (context variable emplaced by [Renderer](service-Renderer.md)) · Parameters: no

The cache of the glTF models: a context variable that loads a file once (`loadGLTF`,
[`gltf_loader.cpp`](../../../src/renderer/model/gltf_loader.cpp)) and hands out the entity that holds its
`ModelC` (GPU vertex and index buffers, parts, materials, bounds). Instances are other entities with a
`Transform` and a `ModelRenderable` pointing at that entity, drawn by the `model` pass. Meshes built in code
(the capsules of `WorldView`, primitive meshes) are cached the same way under a key.

## Behaviour

- **load(path)**: returns the cached entity of `path` if any. Else creates an entity and emplaces on it the
  `ModelC` that `loadGLTF(reg, path)` returns:
  - `.glb` binary or `.gltf` ASCII through tinygltf; tinygltf's warnings logged as `glTF : <warning>`;
  - materials: base colour factor and base colour texture (RGBA8 images only, uploaded once each as sRGB through
    the transfer queue; other images log `glTF : unsupported image ...` and the material is untextured), plus
    one default material for primitives without one;
  - the default scene's node hierarchy is walked and its transforms baked into the positions and normals (all
    meshes untransformed when the file has no scene); non-triangle primitives are skipped (warning);
  - one vertex buffer (`ModelVertex`: position, normal, uv) and one index buffer, with the usage the ray
    structures need where ray tracing is on (`Context::rayInputUsage`); bounds `min` / `max`;
  - logs `Loaded <path> : <n> vertices, <m> triangles`.
  On any exception (malformed file, `no triangles`) logs `Could not load model <path> : <what>` and returns
  `entt::null`; the entity made before the load stays, empty (it is not destroyed) and the path is not cached,
  so a later `load` tries again.
- **add(key, mesh)**: returns the cached entity of `key`, else uploads the `MeshData` (positions, normals, uvs or
  0) as one part with one white material, on a new entity, cached under `key`.
- **get(path)**: the cached entity or `entt::null`, no loading.
- Lifetime: emplaced in the `Renderer`'s constructor; `Renderer::finish` clears every `ModelC` and erases it.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| ModelC | component | write+create | load, add | the mesh of a file or a key, on an entity it creates |
| Context | context | read | load (`loadGLTF`), add | `Context*`: the transfer queue that uploads buffers and images, the device |

## Side effects

- Entities: one per model file or key (never destroyed by it; `Renderer::finish` clears their `ModelC`).
- Files: reads the glTF / glb file and its external buffers and images (tinygltf).
- GPU: vertex, index and texture uploads through `Transfer` (flushed with the frame's transfer commands, or by
  `Renderer::init` for what is loaded during init).
- Log: `glTF : ...` warnings, `Loaded ...` (info), `Could not load model ...` (error).

## Order and dependencies

- Exists from the `Renderer`'s constructor on: systems' constructors and `init` may load models
  (`BaseScene`, `VRControl`, `Spawn`, the world views). Main thread only (uploads go through `Transfer`).

## Used in scenes
<!-- generated -->

## Tests

- The visual tests of the base scene (`V0.*`, `V1.1`, `V2.*`, `P1 check`: boxes thrown and spawned are glTF models).
