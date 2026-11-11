# TerrainSys

> **Kind:** system · **Registry name:** `Terrain` · **Scope:** Host · **Lane:** Post (scenes list it in `host.post`, after `WorldView`) · **Needs renderer:** yes
> **Source:** [`src/renderer/terrain/terrain_sys.h`](../../../src/renderer/terrain/terrain_sys.h), [`src/renderer/terrain/terrain_sys.cpp`](../../../src/renderer/terrain/terrain_sys.cpp)

## Purpose
Turns chunk density volumes into triangle meshes on the GPU. Each tick it picks up to 8 chunks waiting for a mesh (`ChunkMeshPending`), nearest to the desktop camera first, copies their densities into a mapped staging ring and records the marching cubes compute shader into the frame's compute command buffer. Meshes never come back to the CPU: each chunk gets a slot holding a vertex buffer and an indirect draw command whose `vertexCount` the shader fills. It also destroys chunk entities flagged `ChunkDestroying` and recycles their slots once no frame in flight can read them. `TerrainPass` and `GrassPass` draw the result through the `TerrainBuffers` context variable.

## Lifecycle
- **Construction / params:** `TerrainSys(entt::registry& reg)`, registered with `systemInfo<TerrainSys>(SystemScope::Host, true)`: no JSON params (any `params` object is ignored). Constants: `max_per_frame = 8`, `max_slots = 2048`, `max_triangles = 1 << 15` (32768 triangles, 98304 vertices per slot).
- **preinit:** not overridden.
- **init:**
  1. Gets `Context*`; creates the `ChunkGPU` and `ChunkReady` storages so the passes can view them from worker threads.
  2. Alignment = max(`minStorageBufferOffsetAlignment`, 16).
  3. Indirect buffer: `max_slots` `vk::DrawIndirectCommand` at `indirect_stride` (16 bytes rounded up to the alignment), GPU only, storage | indirect | transfer dst.
  4. Staging ring: `compute.ringSize()` (3) x `max_per_frame` x `staging_stride` (67³ floats rounded up), persistently mapped CPU-to-GPU storage buffer.
  5. Reads `./resources/mclookup.bin`; throws `std::runtime_error` unless it is exactly 256 x 16 bytes. Uploads it to a mapped uniform texel buffer viewed as `R8_SINT`.
  6. Descriptor pool (1 + 2048 sets), set layouts (set 0: lookup texel buffer; set 1: vertices SSBO, indirect SSBO, values dynamic SSBO), lookup set.
  7. Compute pipeline from `./resources/shaders/marchingcubes.comp.glsl.spv`, specialization constants local size (8, 4, 8) and `values_per_side = chunk::values` (67), push constants `MCPushConstants { ivec3 pos; int size; uint max_vertices; uint mode; }`.
  8. Emplaces `TerrainBuffers { indirect, indirect_stride }`.
- **tick(dt):** (`dt` unused)
  1. `frame_counter++`. Every pending free whose `frame_counter - frame > NUM_FRAMES + 1` (more than 4 ticks old) goes back to `free_slots`.
  2. Every entity with `ChunkDestroying` and without `ChunkGenerating`: if it has a `ChunkGPU`, its slot is queued in `pending_frees`; the entity is destroyed.
  3. Center = `Camera::position` if the `Camera` context variable exists, else the origin.
  4. Collects entities with `ChunkC`, `ChunkVolume`, `ChunkMeshPending` and without `ChunkDestroying`, keyed by squared distance from `ChunkC::center()` to the center. Returns if none.
  5. Partial-sorts and keeps the nearest `min(count, 8)`.
  6. Gets the compute command buffer (this marks the compute batch non-empty) and `ringIndex()`.
  7. For each chosen chunk: `allocateSlot()` (free list first, else a new slot with a 3 MiB GPU vertex buffer and descriptor set, appended to `TerrainBuffers::vertices`; `UINT32_MAX` once 2048 slots exist). On `UINT32_MAX` logs `Terrain : out of chunk slots` and stops the loop. Otherwise copies `ChunkVolume::values` to staging entry `ring * 8 + i` and records `updateBuffer` resetting the slot's indirect command to `(0, 1, 0, 0)`.
  8. Returns if no build was recorded.
  9. Barrier transfer write -> compute read/write; binds the pipeline and set 0; per build binds set 1 with the dynamic staging offset, pushes `pos = ChunkC::pos * chunk::length`, `size = chunk::cube_size`, `max_vertices = 98304`, `mode = 0`, dispatches 64/8 x 64/4 x 64/8 = 8 x 16 x 8 groups.
  10. Barrier compute write -> compute read/write; per build a 1x1x1 dispatch with `mode = 1` clamps `vertexCount` to `max_vertices`.
  11. Per build: queues the chunk's previous `ChunkGPU` slot (re-mesh) for deferred free, `emplace_or_replace<ChunkGPU>(slot)`, `emplace_or_replace<ChunkReady>()`, `remove<ChunkMeshPending>()`.
- **finish:** not overridden. The destructor waits for device idle, clears `ChunkGPU` and `ChunkReady`, erases `TerrainBuffers`, frees slots and destroys the pipeline, layouts, pool and lookup view. `Game::~Game` resets the system set before the renderer so this runs while the device exists.

## Expected behavior
- At most 8 chunks are meshed per tick; the nearest to the desktop camera (or the origin when no `Camera`) go first. Distance to the VR rig is not considered.
- A chunk that gets `ChunkReady` this tick is drawable this frame: the graphics submit waits on the compute semaphore of the same frame.
- After meshing, `ChunkMeshPending` is gone and `ChunkGPU`/`ChunkReady` are present. Re-adding `ChunkMeshPending` re-meshes the chunk into a new slot; the old slot is reused only after more than 4 further ticks.
- Chunks with `ChunkDestroying` are not destroyed while `ChunkGenerating` is still present.
- Slots are never released back to the GPU allocator, only recycled; the total is capped at 2048 (about 6 GiB of vertex buffers at the cap).
- A chunk producing more than 98304 vertices is truncated (writes past capacity are skipped and `vertexCount` clamped), not corrupted.
- If the slot allocation fails for the first chosen chunk, the compute command buffer has still been marked non-empty, so an empty compute batch is submitted that frame (harmless).
- Pending chunks without `ChunkVolume` (e.g. `ChunkEmpty` chunks) are never meshed by this system.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Context*`](../components/Context.md) | host | read | init, tick, destructor | Device, compute command buffer and ring index |
| [`ChunkGPU`](../components/ChunkGPU.md) | host | write | init | Storage created |
| [`ChunkReady`](../components/ChunkReady.md) | host | write | init | Storage created |
| [`TerrainBuffers`](../components/TerrainBuffers.md) | host | write +emplace_ctx | init | `indirect`, `indirect_stride` |
| [`ChunkDestroying`](../components/ChunkDestroying.md) | host | read +destroy | tick | Entities destroyed |
| [`ChunkGenerating`](../components/ChunkGenerating.md) | host | read | tick | Exclusion filter for destruction |
| [`Camera`](../components/Camera.md) | host | read | tick | `position` (optional, `find`) |
| [`ChunkC`](../components/ChunkC.md) | host | read | tick | `center()`, `pos` |
| [`ChunkVolume`](../components/ChunkVolume.md) | host | read | tick | `values` copied to staging |
| [`ChunkMeshPending`](../components/ChunkMeshPending.md) | host | read +remove | tick | Selection; removed after dispatch |
| [`ChunkGPU`](../components/ChunkGPU.md) | host | readwrite +create | tick | Old slot queued, new slot set |
| [`ChunkReady`](../components/ChunkReady.md) | host | write +create | tick | Emplaced on dispatched chunks |
| [`TerrainBuffers`](../components/TerrainBuffers.md) | host | readwrite | tick | New slot vertex buffers appended |
| [`ChunkGPU`](../components/ChunkGPU.md) | host | write +remove | destructor | `reg.clear` |
| [`ChunkReady`](../components/ChunkReady.md) | host | write +remove | destructor | `reg.clear` |
| [`TerrainBuffers`](../components/TerrainBuffers.md) | host | write +remove | destructor | `ctx().erase` |

## Side effects
- Reads `./resources/mclookup.bin` and the compute shader SPIR-V in `init` (throws on a bad lookup table or missing shader).
- GPU allocations: indirect buffer, staging ring, lookup buffer at init; one vertex buffer (3 MiB) and descriptor set per new slot during ticks.
- Records `updateBuffer`, two pipeline barriers and 2 dispatches per chunk into `Context::compute`'s current command buffer; the Renderer submits it.
- Writes into the persistently mapped staging ring (CPU memcpy of 67³ floats per chunk).
- Destroys chunk entities.
- Logs a warning when out of slots. Destructor blocks on `waitIdle`.

## Ordering and dependencies
- Must run after `ChunkManagerSys` / `ChunkGeneratorSys` have produced `ChunkVolume` + `ChunkMeshPending` (they are earlier in the frame), and in the same frame before `Renderer::render`, which flushes the compute batch and records the passes.
- Staging entries are indexed by `Compute::ringIndex()`; that command buffer's previous use was fenced in `Compute::prepare`, so overwriting its staging region is safe.
- Runs on the main thread; the passes only read `ChunkGPU`/`ChunkReady`/`TerrainBuffers` later in the frame during parallel recording.
- Needs the `Context*` context variable (created by the Renderer before any system).

## Related
[TerrainPass](TerrainPass.md), [GrassPass](GrassPass.md), [Renderer](Renderer.md), [`src/renderer/terrain/`](../folders/src-renderer-terrain.md), [`src/renderer/shaders/`](../folders/src-renderer-shaders.md) (`marchingcubes.comp.glsl`), chunk components in `src/logic/terrain/chunk.h`.
