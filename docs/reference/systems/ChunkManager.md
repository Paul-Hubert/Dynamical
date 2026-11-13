# ChunkManager

`ChunkManagerSys` in [`src/logic/systems/chunk_systems.cpp`](../../../src/logic/systems/chunk_systems.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre` · Parameters: no

Streams the marching cubes world: creates a chunk entity (`ChunkC`) for every chunk whose centre is within the
view distance of a viewer (the desktop camera, and the VR rig while a headset session runs), and tags chunks that
went too far with `ChunkDestroying` so that the renderer destroys them once the GPU and the workers are done with
them. It never destroys an entity itself and never fills densities (that is [ChunkGenerator](ChunkGenerator.md)).

## Behaviour

- **preinit**: emplaces the `ChunkMap` context variable (chunk coordinates to chunk entity), empty.
- **tick**:
  1. The view distance is `Settings::view_distance` when the `Settings` context variable exists (dynamical),
     else `Settings::default_view_distance` (768 world units) in headless runs.
  2. Load centres (`loadCenters`): the `Camera` context variable's position when there is one, plus the `VRRig`
     position when `VRInput` and `VRRig` exist and `VRInput::session_running` is true. Without any centre the tick
     does nothing.
  3. **Unload**: every chunk of `ChunkMap` whose centre (`ChunkC::center()`, origin + 128 units) is farther than
     `view_distance + chunk::length` (256 units of hysteresis, so chunks on the edge do not flicker) from every centre
     gets `ChunkDestroying` (`emplace_or_replace`) and is erased from the map at once. The entity stays alive.
  4. **Load**: for every centre, every chunk coordinate in the cube of `ceil(view_distance / 256)` chunks around the
     centre's chunk that is not in the map and whose centre is within `view_distance` of the nearest centre gets a
     new entity with `ChunkC(pos)`, recorded in the map.
- No preinit dependency on other systems; `ChunkMap` exists from preinit on, so systems that read it later in
  preinit or in init find it.

After a tick, `ChunkMap` holds exactly the chunks in range (plus those within the hysteresis band), each a live
entity with a `ChunkC`; chunks out of range are no longer in the map and carry `ChunkDestroying`.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [ChunkMap](../components/ChunkMap.md) | context | write+create | preinit | emplaced empty |
| [ChunkMap](../components/ChunkMap.md) | context | readwrite | tick | chunks added on load, erased on unload |
| [Settings](../components/Settings.md) | context | read | tick | `view_distance` (default 768 without it) |
| [Camera](../components/Camera.md) | context | read | tick (`loadCenters`) | desktop camera position, a load centre |
| [VRInput](../components/VRInput.md) | context | read | tick (`loadCenters`) | `session_running`: the rig is a load centre only then |
| [VRRig](../components/VRRig.md) | context | read | tick (`loadCenters`) | rig position, the second load centre |
| [ChunkC](../components/ChunkC.md) | component | read+create | tick | made on new chunk entities; read for the centre of loaded chunks |
| [ChunkDestroying](../components/ChunkDestroying.md) | tag | write+create | tick | put on chunks out of range |

## Side effects

- Creates one entity per chunk coming into range (none destroyed here: `TerrainSys` destroys the `ChunkDestroying`
  ones that are not `ChunkGenerating` any more).
- Chunk count around one viewer at the default distance: chunks whose centres lie in a sphere of 768 units, about
  (2·3+1)³ coordinates tested per centre per frame.

## Order and dependencies

- Before [ChunkGenerator](ChunkGenerator.md) in the same lane (base: `ChunkManager`, `ChunkGenerator`), so new
  chunks start generating on the frame they appear.
- After whatever moves the `Camera` and `VRRig` this frame (`PlayerControl`, `VRControl`, `DesktopView`, camera
  walks), so chunks follow this frame's position.
- `TerrainSys` (`Terrain`, `host.post`) consumes the `ChunkDestroying` tag; `ForestSys`, `VegetationScatter`,
  `TerrainColliders` (`ChunkMap` lookups) and the lighting passes read the chunk entities.

## Used in scenes
<!-- generated -->

## Tests

- `T2.4 base system list after the PhysicsSys split` ([`tests/unit/base_scene.cpp`](../../../tests/unit/base_scene.cpp)): position in the base list.
- Visual and probe tests of the base scene (`V0.1`, `V0.2`, `S0.1`, `S1.1`, `V2.2` in [`CMakeLists.txt`](../../../CMakeLists.txt), [`tests/capture/base.capture.json`](../../../tests/capture/base.capture.json)): the streamed terrain in the goldens.
