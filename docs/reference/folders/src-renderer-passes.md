# `src/renderer/passes/`

> [Source](../../../src/renderer/passes/) · Parent: [`src/renderer/`](src-renderer.md)

## Summary
The scene passes, each a `ScenePass` with one pipeline per render target. The Renderer instantiates those listed in the scene's `renderer.passes` (`terrain`, `grass`, `model`, `ui`) in that order, which is the draw order. `update()` runs on the main thread before recording; `record()` runs concurrently for the desktop and VR targets and only reads the host registry through a `const entt::registry&`.

## Files
| File | Contents |
|---|---|
| `terrain_pass.h` / `terrain_pass.cpp` | `TerrainPass` (indirect draws of `ChunkGPU` + `ChunkReady` chunks, grass/rock array texture); `terrainVertexBindings()` / `terrainVertexAttributes()` shared with the grass pass |
| `grass_pass.h` / `grass_pass.cpp` | `GrassPass` (raymarched grass shells over the chunk meshes; `raycast_data.bin` 3D lookup, 512² tiled noise, `GrassPC` push constants from `RenderSettings::grass_params`) |
| `raycast_data.h` | `Sample` (`u8vec4`), `RaycastData` (32 angles x 128 x 128 samples, cereal-serialized), shared with `tools/raycast` |
| `model_pass.h` / `model_pass.cpp` | `ModelPass` (material descriptor sets in `update`, draws `Transform` + `ModelRenderable` instances of `ModelC` meshes, `ModelPC` push constants, 1024-material pool) |
| `ui_pass.h` / `ui_pass.cpp` | `UIPass` (Dear ImGui, desktop only: font atlas upload, `ImGui::Render` in `update`, per-frame mapped vertex/index buffers, clipped draws) |

## Systems defined here
- [TerrainPass](../systems/TerrainPass.md) (pass)
- [GrassPass](../systems/GrassPass.md) (pass)
- [ModelPass](../systems/ModelPass.md) (pass)
- [UIPass](../systems/UIPass.md) (pass)

## Components defined here
None.

## Subfolders
None.
