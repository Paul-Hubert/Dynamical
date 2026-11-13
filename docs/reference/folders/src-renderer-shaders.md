# src/renderer/shaders

The GLSL shaders of the renderer. The CMake build compiles every `*.glsl` of this folder (not `include/`) to
SPIR-V with `glslangValidator --target-env vulkan1.1` (subgroup operations) into `resources/shaders/<file>.spv`;
the scene fragment shaders (those that include `include/scene.glsl`, not `*.rt.*`) are compiled a second time with
`-DRAY_TRACING` into `<file>.rt.spv`, and `sceneFragmentShader()` ([`util/pipeline.h`](../../../src/renderer/util/pipeline.h))
picks one by `ctx.device.ray.enabled()`. `*.rt.*.glsl` only exist with ray tracing. Every scene pipeline has the view
set (set 0: `ViewUBO` binding 0, tree shade 1, soil maps 2, lights 3, the ray bindings 4 to 12) and its fragment
shader ends with `writeLit(unlit, sunlit, shade, position, alpha)`.

## Files, by pass

| Pass / owner | File | What |
|---|---|---|
| Terrain system | [`marchingcubes.comp.glsl`](../../../src/renderer/shaders/marchingcubes.comp.glsl) | marching cubes over one chunk, an invocation a cube: appends triangles to the slot's vertex buffer, vertex count in its indirect draw; area-weighted vertex normals (`vertexNormal`), ground curve; a second mode clamps overflowing counts |
| `sky` | [`sky.vert.glsl`](../../../src/renderer/shaders/sky.vert.glsl) | one triangle over the screen at the far plane |
| `sky` | [`sky.frag.glsl`](../../../src/renderer/shaders/sky.frag.glsl) | sky colour (`include/view.glsl`), discs of the sun and the moon, stars at night |
| `terrain` | [`terrain.vert.glsl`](../../../src/renderer/shaders/terrain.vert.glsl) | `TerrainVertex` (octahedral normal decoded, curve), world position and normal |
| `terrain` | [`terrain.frag.glsl`](../../../src/renderer/shaders/terrain.frag.glsl) | the ground: earth under the grass, soil patches by the soil map (`soilWarp`), rock on the steep faces, scree, relief (`reliefAt`, `soilLayer`), slope grass (`slopeGrass`) |
| `grass` | [`grass.vert.glsl`](../../../src/renderer/shaders/grass.vert.glsl) | the shell at the top of the plants: height map layer of the chunk, slope and soil lowering, `v_cover` |
| `grass` | [`grass.frag.glsl`](../../../src/renderer/shaders/grass.frag.glsl) | the view ray looked up in the baked rays (oblique 2D arrays, top rays), species layers, baked colour, see-through on crests, blades dropped near soil patches |
| `grass` | [`grass_back.frag.glsl`](../../../src/renderer/shaders/grass_back.frag.glsl) | back faces of the shell: marks the stencil where the shell is seen from behind, in front of the ground |
| `vegetation` | [`vegetation.vert.glsl`](../../../src/renderer/shaders/vegetation.vert.glsl) | SpeedTree geometry of the library, instanced, no wind |
| `vegetation` | [`vegetation.frag.glsl`](../../../src/renderer/shaders/vegetation.frag.glsl) | alpha test, normal map, baked occlusion, subsurface, tree shade |
| `vegetation` (ray tracing) | [`vegetation_volume.vert.glsl`](../../../src/renderer/shaders/vegetation_volume.vert.glsl) | the box of a far plant, twelve triangles without a vertex buffer |
| `vegetation` (ray tracing) | [`vegetation_volume.rt.frag.glsl`](../../../src/renderer/shaders/vegetation_volume.rt.frag.glsl) | a far plant marched through the baked volume of its species, in place of its billboard |
| `animals` | [`animal.vert.glsl`](../../../src/renderer/shaders/animal.vert.glsl) | skinned meshes, four joints a vertex, matrices of the frame from a storage buffer (set 2) |
| `animals` | [`animal.frag.glsl`](../../../src/renderer/shaders/animal.frag.glsl) | the view, the lights, the tree shade, as the vegetation |
| `animals` (ray tracing) | [`animal_skin.rt.comp.glsl`](../../../src/renderer/shaders/animal_skin.rt.comp.glsl) | skins an animal near the viewers into the buffer its acceleration structure is built from |
| `model` | [`model.vert.glsl`](../../../src/renderer/shaders/model.vert.glsl) | `ModelVertex` (position, normal, uv), per-instance model matrix |
| `model` | [`model.frag.glsl`](../../../src/renderer/shaders/model.frag.glsl) | base colour texture of the material (white when untextured), lit |
| `ui` | [`uirender.vert.glsl`](../../../src/renderer/shaders/uirender.vert.glsl) | ImGui vertices, scale and translate push constants |
| `ui` | [`uirender.frag.glsl`](../../../src/renderer/shaders/uirender.frag.glsl) | vertex colour x font / image texture |
| `SunVisibility` (ray tracing) | [`fullscreen.vert.glsl`](../../../src/renderer/shaders/fullscreen.vert.glsl) | one triangle over the screen |
| `SunVisibility` (ray tracing) | [`sun_visibility.rt.frag.glsl`](../../../src/renderer/shaders/sun_visibility.rt.frag.glsl) | rays from the point under each pixel to the disc of the sun or moon, within the traced range |
| `SunVisibility` (ray tracing) | [`sun_resolve.frag.glsl`](../../../src/renderer/shaders/sun_resolve.frag.glsl) | the visibility filtered over time (reprojected by the depth) |
| `SunVisibility` (ray tracing) | [`composite.frag.glsl`](../../../src/renderer/shaders/composite.frag.glsl) | colour + sun x visibility into the target's image, the visibility smoothed over the same surface |
| `ProbeVolume` (ray tracing) | [`probe_update.rt.comp.glsl`](../../../src/renderer/shaders/probe_update.rt.comp.glsl) | the probes of a brick: first look, rays, L1 light per colour, sun and sky seen |
| `PlantVolumes` (ray tracing) | [`plant_bake.rt.comp.glsl`](../../../src/renderer/shaders/plant_bake.rt.comp.glsl) | bakes the density, colour and normal volume of a species against its most detailed level |

### include/ (not compiled on their own)

| File | What |
|---|---|
| [`include/view.glsl`](../../../src/renderer/shaders/include/view.glsl) | set 0: the view (`ViewUBO`) and the lights of the frame, the sky that follows (`skyColor`, `skyRadiance`, mirrors `logic/sky/sky.h`) |
| [`include/scene.glsl`](../../../src/renderer/shaders/include/scene.glsl) | what every scene fragment shader shares: view, lights, tree shade, point lights, `writeLit`; with `RAY_TRACING` the torches' rays and the probes |
| [`include/tree_shade.glsl`](../../../src/renderer/shaders/include/tree_shade.glsl) | `treeShade()`: the crown shadows of `TreeShade` (binding 1), flecks of sun from a noise |
| [`include/rays.glsl`](../../../src/renderer/shaders/include/rays.glsl) | rays against the scene with ray queries (`RayScene`): the geometry table, alpha tested leaves, `traceShadow` |
| [`include/probes.glsl`](../../../src/renderer/shaders/include/probes.glsl) | reading the irradiance probes (`probeLight()`), only with `RAY_TRACING` |

## Systems

None registered here (shaders). Users: [Terrain](../systems/Terrain.md), the passes ([pass-sky](../systems/pass-sky.md),
[pass-terrain](../systems/pass-terrain.md), [pass-grass](../systems/pass-grass.md), [pass-vegetation](../systems/pass-vegetation.md),
[pass-animals](../systems/pass-animals.md), [pass-model](../systems/pass-model.md), [pass-ui](../systems/pass-ui.md)) and the lighting
([service-SunVisibility](../systems/service-SunVisibility.md), [service-ProbeVolume](../systems/service-ProbeVolume.md),
[service-PlantVolumes](../systems/service-PlantVolumes.md)).

## Components and context variables

None.

## Mirrored on the CPU

- `marchingcubes.comp.glsl` ↔ `MarchingCubes::vertexNormal`, `groundCurve` (`src/logic/terrain`).
- `grass.frag.glsl` ↔ `lookupGrass`, `grassSpeciesAt`, `grassSeeThrough` (`src/logic/grass`).
- `include/view.glsl` ↔ `Sky`, `skyLight` (`src/logic/sky`).

## Design docs

[docs/lighting.md](../../lighting.md), [docs/ground.md](../../ground.md), [docs/ground-relief.md](../../ground-relief.md),
[docs/grass.md](../../grass.md) and [docs/grass/](../../grass/plan.md), [docs/vegetation.md](../../vegetation.md),
[docs/animals.md](../../animals.md).
