# `src/renderer/shaders/`

> [Source](../../../src/renderer/shaders/) · Parent: [`src/renderer/`](src-renderer.md)

## Summary
GLSL 450 sources compiled by CMake to SPIR-V with `glslangValidator --target-env vulkan1.1` and loaded at runtime from `./resources/shaders/<name>.spv`. Every scene shader reads `ViewUBO` at set 0, binding 0 (`viewproj`, `viewpos` with time in `w`). The compute shader is dispatched by `TerrainSys`; the graphics pairs belong to the passes.

## Files
| File | Contents |
|---|---|
| `marchingcubes.comp.glsl` | Marching cubes over one chunk, one invocation per cube (local size from specialization constants, 8x4x8). Reads densities (67³ floats, `[x][z][y]`, > 0 solid) and the `mclookup.bin` edge table; appends triangles with one subgroup atomic per subgroup into the slot's vertex buffer (2 vec4 per vertex: position + normal + uv) and the indirect `vertexCount`. Normals from the density gradient, uv = xz / 32. `mode = 1` clamps `vertexCount` to capacity. Needs subgroup arithmetic and ballot |
| `terrain.vert.glsl` | Passes world position, normal and uv of terrain vertices; projects with `viewproj` |
| `terrain.frag.glsl` | Triplanar rock (layer 1) on the sides, grass (layer 0) on top weighted by normal.y³, scale 1/3; sun lighting (0.3 ambient), exponential distance fog to sky color |
| `grass.vert.glsl` | Terrain mesh lifted along +Y by the grass height (shell) |
| `grass.frag.glsl` | Raymarched grass through 3 layers using the 3D blade lookup (`raycast_data.bin`) and tiled noise; discards where the surface normal.y < 0.75 |
| `model.vert.glsl` | Model matrix from push constants, world position/normal/uv out |
| `model.frag.glsl` | Base color texture x push constant color (material color x tint), sun lighting, fog |
| `uirender.vert.glsl` | ImGui vertices, scale/translate push constants to clip space |
| `uirender.frag.glsl` | Vertex color x font/texture sample |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
