# Terrain

`TerrainSys` in [`src/renderer/terrain/terrain_sys.cpp`](../../../src/renderer/terrain/terrain_sys.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.post · Parameters: no

The GPU half of the terrain: turns the densities of the chunks that wait for a mesh into triangles with the marching
cubes compute shader (`marchingcubes.comp.glsl`), in the compute command buffer of the frame. Meshes stay on the GPU:
one vertex buffer and one indirect draw command per chunk slot ([TerrainBuffers](../components/TerrainBuffers.md)),
which TerrainPass, GrassPass and the ray scene read. It also destroys the chunk entities that ChunkManager marked for
destruction, and recycles their slots once no frame in flight reads them.

## Behaviour

- **init**: creates the storages of `ChunkGPU`, `ChunkReady`, `ChunkTriangles` (so render passes can view them from
  worker threads); reads whether ray queries are enabled; allocates the indirect buffer (2048 slots, stride aligned to
  the storage offset alignment), the readback buffer (ray tracing only), the density staging ring
  (`compute.ringSize() x 8` chunks of 67³ floats), the marching cubes edge table (`resources/mclookup.bin`, 4096 bytes,
  as an R8 texel buffer); descriptor pool and layouts; the compute pipeline (local size 8 x 4 x 8, specialised on the
  value count); emplaces `TerrainBuffers` (indirect buffer and stride).
- **tick**:
  1. Frees the slots of meshes released more than `NUM_FRAMES + 1` frames ago.
  2. With ray tracing: for the meshes counted at least `NUM_FRAMES` frames ago, reads the vertex count back and sets
     `ChunkTriangles { count / 3, slot }` on the chunk when it still has that slot.
  3. Destroys every entity with `ChunkDestroying` and without `ChunkGenerating`, queuing its slot for release.
  4. Takes the chunks with `ChunkC`, `ChunkVolume`, `ChunkMeshPending` (not destroying), nearest to the camera first,
     at most `RenderSettings::streaming.chunk_meshes` (clamped to 1..8; 8 without settings).
  5. For each: allocates a slot (reuses a free one, else a new vertex buffer of 2^15 triangles x 3 vertices x 32
     bytes and its descriptor set, appended to `TerrainBuffers::vertices`; out of 2048 slots: warning, stop), copies
     the densities into the staging ring, resets the slot's indirect command (0 vertices, 1 instance).
  6. Records: barrier, one dispatch per chunk (push constants: world position, cube size, max vertices), barrier, a
     one-thread pass per chunk that clamps an overflowed vertex count; with ray tracing, copies the indirect commands to
     the readback buffer and a barrier for the acceleration structure builds that follow.
  7. Per chunk: releases its previous slot, `ChunkGPU { slot }`, removes `ChunkTriangles`, adds `ChunkReady`, removes
     `ChunkMeshPending`. The graphics submit waits on this compute submit, so the mesh is drawable this frame.
- **Destructor**: waits for the device, clears `ChunkGPU`, `ChunkReady`, `ChunkTriangles`, erases `TerrainBuffers`,
  frees the slots and Vulkan objects.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Context](../components/Context.md) | context | read | init, tick, destructor | the device, VMA and the compute command buffer of the frame; stored as Context* |
| [TerrainBuffers](../components/TerrainBuffers.md) | context | readwrite+create+destroy | init (emplace), allocateSlot, destructor (erase) | the indirect buffer and its stride; a vertex buffer appended per new slot |
| [ChunkGPU](../components/ChunkGPU.md) | component | readwrite+create+destroy | init (storage), tick, destructor (clear) | slot of a chunk's mesh: the old slot is freed when remeshed or destroyed |
| [ChunkReady](../components/ChunkReady.md) | tag | readwrite+create+destroy | init (storage), tick, destructor (clear) | set when the mesh is in this frame's compute submit: drawable |
| [ChunkTriangles](../components/ChunkTriangles.md) | component | readwrite+create+destroy | init (storage), tick, destructor (clear) | removed when remeshed; with ray tracing, set NUM_FRAMES later from the read back vertex count |
| [ChunkMeshPending](../components/ChunkMeshPending.md) | tag | read+destroy | tick | the chunks to mesh; removed once dispatched |
| [ChunkC](../components/ChunkC.md) | component | read | tick | centre (nearest first) and pos (push constants) |
| [ChunkVolume](../components/ChunkVolume.md) | component | read | tick | the 67^3 densities copied into the staging ring |
| [ChunkDestroying](../components/ChunkDestroying.md) | tag | read+destroy | tick | chunks out of view distance (without ChunkGenerating): their slot is freed later and the entity destroyed |
| [ChunkGenerating](../components/ChunkGenerating.md) | tag | read | tick | excluded: a chunk a worker still writes is not destroyed yet |
| [Camera](../components/Camera.md) | context | read | tick | position: the nearest chunks are meshed first (origin without a camera) |
| [RenderSettings](../components/RenderSettings.md) | context | read | tick | streaming.chunk_meshes: chunks meshed a frame (1 to max_per_frame = 8) |

## Side effects

- Reads ./resources/mclookup.bin (throws when it is not 256 x 16 bytes) and ./resources/shaders/marchingcubes.comp.glsl.spv in init.
- Allocates GPU memory: indirect buffer (2048 slots), staging ring (ring size x 8 chunks), lookup texel buffer, a 3 MB vertex buffer per slot (2^15 triangles x 3 vertices x 32 bytes, ray tracing usages when enabled), a readback buffer with ray tracing; descriptor pool and sets, compute pipeline.
- Records into the compute command buffer of the frame: indirect resets, one dispatch per chunk, a clamp pass, with ray tracing copies of the indirect commands to the readback buffer; Tracy GPU zone "Marching cubes", CPU zone "Mesh chunks", plot "Chunks meshed".
- Destroys the entities of chunks marked ChunkDestroying (all their components go with them).
- Logs a warning "Terrain : out of chunk slots" when 2048 slots are used.
- The destructor waits for the device to be idle and clears its components and TerrainBuffers.

## Order and dependencies

- In host.post: after ChunkManager (marks `ChunkDestroying`) and ChunkGenerator (fills `ChunkVolume`, sets
  `ChunkMeshPending` on its taskflow workers, merged on the main thread), and after the systems that move the camera.
- Records into the compute command buffer before `renderer->render()`; the ray scene (RayScene) records after it in the
  same buffer and reads the vertex buffers and `ChunkTriangles`.
- TerrainPass, GrassPass, SoilMaps and the grass height maps key on `ChunkGPU::slot` and `ChunkReady`.
- Captures wait on `ChunkMeshPending` (`terrainReady`, see [service-Capture](service-Capture.md)).

## Used in scenes

<!-- generated -->

## Tests

- `T2.4 base system list ...` (stub: its place in host.post). GPU: every capture of a terrain scene (`V0.x`, `V1.1`,
  `V2.2`, the lighting and grass shots). The marching cubes of the shader are mirrored on the CPU by
  `MarchingCubes`, tested by `TERRAIN.*` (`tests/unit/terrain_normals.cpp`).
