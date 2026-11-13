# ChunkLayers

`ChunkLayers` in [`src/renderer/terrain/chunk_layers.h`](../../../src/renderer/terrain/chunk_layers.h) · Scope: Host · Needs renderer: yes
· Lanes: none (a member of `SoilLayers`, made by [SoilMaps](service-SoilMaps.md), and of [GrassPass](pass-grass.md)) · Parameters: no

Which layer of a 2D array image holds the map of each chunk: the soil map ([SoilMaps](service-SoilMaps.md)) and
the height map of the grass (`GrassPass`). A layer is given to a chunk when it gets its mesh, before it is
drawn, and taken back `NUM_FRAMES + 1` frames after the chunk is gone or its map was baked again, when no frame in
flight reads it any more. Chunks are known by their mesh slot of the `Terrain` system (`ChunkGPU::slot`) and
their entity.

## Behaviour

- **Constructor(layers)**: every layer `0 .. layers - 1` free.
- **update(reg, slots, key, max_rebakes, bake)** (main thread, once a frame, by its owner):
  1. Frame counter + 1; retired layers older than `NUM_FRAMES + 1` frames are free again.
  2. One map record per chunk slot (grown to `slots`).
  3. For every entity with `ChunkGPU` and `ChunkReady`: marks its slot seen; if the slot went to another chunk,
     the old layer is retired; if the map has no layer, or was baked with another `key` and fewer than
     `max_rebakes` re-bakes were made this frame, takes a free layer (none left: skipped until one frees) and
     calls `bake(layer, entity)`, retiring the layer it replaces (the chunk keeps its old map until then).
  4. Slots not seen this frame (their chunk is gone): layer retired, record reset.
- **layer(slot, chunk)** (called by the passes while they record, from worker threads; const): the layer of
  that chunk's map, -1 when it has none yet, the layers ran out or the slot holds another chunk.

## Parameters

None (the owner gives the layer count, the key of its parameters and the re-bake budget: 16 a frame for both owners).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| ChunkGPU | component | read | update | the mesh slot a map record belongs to |
| ChunkReady | tag | read | update | only chunks with a drawable mesh get a layer |

## Side effects

- None of its own: what `bake` does (CPU bake and `Transfer::updateImage`) is its owner's.

## Order and dependencies

- `update` after the `Terrain` system's tick (which emplaces `ChunkGPU` and `ChunkReady` and destroys gone
  chunks) and before recording; `layer()` must not run concurrently with `update` (the targets record after all
  updates).

## Used in scenes
<!-- generated -->

## Tests

- None directly; the soil and grass maps it places are in the visual tests (`tests/capture/grass*.capture.json`).
