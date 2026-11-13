# RayScene

`RayScene` in [`src/renderer/lighting/ray_scene.cpp`](../../../src/renderer/lighting/ray_scene.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by the `Renderer`, only with ray tracing) · Parameters: no

What the rays of the lighting see ([docs/lighting.md](../../lighting.md),
[docs/lighting/built.md](../../lighting/built.md)): a bottom level acceleration structure per chunk (from the
vertex buffer the marching cubes shader filled, once its triangle count came back), one per mesh the passes
gave (`RayMeshes`: plant species, models), one per animal near the viewers (`RayAnimals`, refitted), a unit
box for the plants far away, and the top level structure, built again every frame in the compute command
buffer of the frame. The geometry table (set 0 binding 5) tells a shader what it hit
(`include/rays.glsl`). Created by the `Renderer` when `ctx.device.ray.enabled()` (the GPU has ray queries and
`ray_tracing` is not false), before the passes.

## Behaviour

**Constructor**: makes sure the `ChunkLit`, `ChunkTriangles`, `PlantInRays`, `PlantRayTransforms` storages
exist; connects `onChunkGone` to `on_destroy<ChunkTriangles>`; emplaces the context variables
`LightingStats`, `RayMeshes` and `RayBake`; writes entry 0 of the geometry table (the terrain kind); creates
the top level structure for 4096 instances (`Context::tlas`), and one instance buffer per compute command
buffer of the ring.

**update()** (main thread, once a frame, after the passes' `update()` and `PlantVolumes::prepare()`, before
the compute commands are submitted):

1. Retired structures and buffers are destroyed `NUM_FRAMES + 1` frames after they were let go; freed table
   entries age.
2. Gets the frame's compute command buffer (GPU zone "Ray scene"), a full barrier after the frames before.
3. `updateChunks()`: without `TerrainBuffers`, `chunks_pending = 0` and nothing. Else removes `ChunkLit`
   from the chunks that lost their `ChunkTriangles` (meshed again). Chunks with `ChunkC` + `ChunkGPU` +
   `ChunkTriangles` (the counted slot is the current one), without `ChunkLit` or `ChunkDestroying`, are
   sorted by distance to the `Camera`; up to `RenderSettings::streaming.ray_chunks` (8) a frame: no
   triangle → `ChunkLit` at once; else a bottom level build from the slot's vertex buffer
   (`TerrainBuffers::vertices[slot]`, stride `TerrainVertex`, fast trace), a geometry table entry (reused
   from a chunk gone long enough, else added), and `ChunkLit`. Stops when the frame's scratch memory is
   spent. Writes `chunks_lit` and `chunks_pending` (chunks with `ChunkReady` still without `ChunkLit`).
4. `updateMeshes()`: points `Context::ray_textures` at `RayMeshes::textures`; a bottom level build per mesh
   of `RayMeshes` not built yet, a geometry per part (opaque, or tested against its texture once), their
   table entries; `structures_pending` = meshes that did not fit this frame. Once: the unit box (an AABB
   geometry). A table entry for each new `RayMeshes::boxes` (kind Volume).
5. `updateAnimals()`: for every `RayAnimals::Animal`: a known animal with the same counts is refitted
   (update build) unless it was built over 240 + (entity % 64) frames ago; a new one gets a build and a table
   entry (Solid, its colour), a changed or old one a new build. Animals no longer listed lose their
   structure.
6. `flushBuilds()`: records every queued bottom level build (GPU zone "Build bottom structures") and a
   barrier.
7. `gatherInstances()`: the chunks (mask Terrain); the viewers (`Camera`, and the headset from `VRRig` x
   `VRInput::head` when the session runs) and the plant range (the larger
   `lighting[target].plant_geometry_range`). With `RayPlantMeshes`: plants without `PlantRayTransforms` get
   one (their matrix rows, once: plants do not move). Per `Transform` + `Plant` + `PlantRayTransforms`: in
   range (entering at the range, leaving at 1.15 x it, `PlantInRays` added / removed) → its species' mesh
   (mask Plants) when built; out of range → the box of its species' volume (mask Volumes) when
   `RayPlantMeshes::box` has one and `far_plants_in_rays`, its rows worked out once per box. Counts
   `plants_near`, `plants_far`. The listed animals (mask Dynamic). The bake mesh of `RayBake` at its origin
   (mask Bake), `placed` set. Every `Transform` + `ModelRenderable` whose model (`ModelC::ray_mesh`) is
   built: mask Static, or Dynamic when it has a `RigidBody` that is not static.
8. `buildTop()`: copies the instances into this ring slot's buffer (grown when needed), grows the top level
   structure when it is too small (`Context::tlas` replaced), records the top level build (GPU zone "Build
   top structure") and a barrier for every later stage; `LightingStats::instances`.

**onChunkGone** (signal, `on_destroy<ChunkTriangles>`): retires the chunk's structure and frees its table
entry.

**Destructor**: disconnects the signal, waits for the device, destroys every structure, clears
`Context::ray_geometries`, `ray_textures`, `tlas`; clears the `PlantInRays`, `PlantRayTransforms`,
`ChunkLit` storages; erases `RayBake` and `RayMeshes`. `LightingStats` is left in the context.

## Parameters

None from the scene. Settings read:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `RenderSettings::streaming.ray_chunks` | int | 8 | chunk structures built a frame |
| `RenderSettings::lighting[0/1].plant_geometry_range` | float | 120 | plants are their leaves in the rays within, a box beyond |
| `RenderSettings::far_plants_in_rays` | bool | true | far plants as boxes in the rays (`far_plants=`) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| ChunkLit | tag | readwrite+create+destroy | updateChunks (add, remove stale), destructor (clear) | chunks in the rays |
| ChunkTriangles | component | read | updateChunks; `on_destroy` signal | triangle count and slot of a chunk's mesh |
| ChunkC | component | read | updateChunks | distance to the camera |
| ChunkGPU | component | read | updateChunks | the slot must be the counted one |
| ChunkDestroying | tag | read | updateChunks | excluded |
| ChunkReady | tag | read | updateChunks | chunks pending |
| TerrainBuffers | context | read | updateChunks | vertex buffers of the slots |
| LightingStats | context | readwrite+create | constructor (emplace), queueBuild, updateChunks, updateMeshes, gatherInstances, buildTop | structure bytes, chunks lit / pending, structures pending, plants near / far, instances |
| RayMeshes | context | readwrite+create+destroy | constructor (emplace), updateMeshes, gatherInstances, destructor (erase) | meshes, boxes and textures the passes give; read after creation |
| RayBake | context | readwrite+create+destroy | constructor (emplace), gatherInstances, destructor (erase) | places the bake mesh, sets `placed` |
| RayAnimals | context | read | updateAnimals | animals skinned this frame |
| RayPlantMeshes | context | read | gatherInstances | mesh and box of each species |
| PlantInRays | tag | readwrite+create+destroy | gatherInstances, destructor (clear) | plants that are their leaves in the rays |
| PlantRayTransforms | component | readwrite+create+destroy | gatherInstances, destructor (clear) | instance rows of a plant's mesh and box |
| Plant | component | read | gatherInstances | species |
| Transform | component | read | gatherInstances | position and matrix of plants and models |
| ModelRenderable | component | read | gatherInstances | the model entity |
| ModelC | component | read | gatherInstances | `ray_mesh` of the model |
| RigidBody | component | read | gatherInstances | `motion`: dynamic models are not static instances |
| Camera | context | read | updateChunks, gatherInstances | build order of the chunks, a viewer |
| VRInput | context | read | gatherInstances | headset running, head position |
| VRRig | context | read | gatherInstances | rig transform |
| RenderSettings | context | read | updateChunks, gatherInstances | see Parameters |

## Side effects

- Builds and refits acceleration structures on the GPU in the frame's compute command buffer; allocates their buffers, the scratch buffer (32 MB first), the geometry table (mapped, grown by doubling) and the instance buffers.
- Writes `Context::tlas`, `Context::ray_geometries`, `Context::ray_textures`, which `Context::writeViewSet` puts in every view set (bindings 4 to 6).
- Connects a handler to `on_destroy<ChunkTriangles>`; the destructor disconnects it and waits for the device.

## Order and dependencies

- After `TerrainSys` (chunks meshed, `ChunkTriangles` read back), and after the passes' `update()` (meshes added to `RayMeshes` by `VegetationPass` and `ModelPass`, `RayAnimals` filled and skinned by `AnimalPass`) and `PlantVolumes::prepare()` (`RayBake`).
- Before `ProbeVolume::update()` (`ChunkLit` decides which chunks get probes) and `PlantVolumes::update()` (`RayBake::placed`), which trace the structure it built in the same compute command buffer.
- Before the targets' `updateView()`, which writes the view sets with the new `tlas`.

## Used in scenes
<!-- generated -->

## Tests

- No unit test (needs ray queries). Shots by hand: `tests/capture/lighting*.capture.json`; captures wait for `LightingStats::settled()`.
