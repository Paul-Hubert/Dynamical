# ChunkManagerSys

> **Kind:** system · **Registry name:** `ChunkManager` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** no
> **Source:** [`src/logic/systems/chunk_systems.h`](../../../src/logic/systems/chunk_systems.h), [`src/logic/systems/chunk_systems.cpp`](../../../src/logic/systems/chunk_systems.cpp)

## Purpose
Streams the marching cubes world on the host registry: every frame it creates a `ChunkC` entity for each chunk whose centre lies within `view_distance` of a load centre (the desktop `Camera`, and the `VRRig` while a VR session runs), records it in the `ChunkMap` context variable, and tags chunks that drifted beyond `view_distance + chunk::length` with `ChunkDestroying` (removing them from the map). Generation is left to [ChunkGeneratorSys](ChunkGeneratorSys.md), meshing and destruction to [TerrainSys](TerrainSys.md). Used by the host `pre` lanes of `base` and the viewer scenes (`sphere_view`, `sphere_view_heuristic`, `vr_roller`, `wheel_view`).

## Lifecycle
- **Construction / params:** `ChunkManagerSys(reg)`, no params (`systemInfo<ChunkManagerSys>(SystemScope::Host)`).
- **preinit:** `reg.ctx().emplace<ChunkMap>()` (empty map).
- **init:** None.
- **tick(dt):** `dt` unused.
  1. `get<ChunkMap>()`; `find<Settings>()`: `view_distance = settings ? settings->view_distance : Settings::default_view_distance` (768 world units).
  2. Load centres (`loadCenters`): `Camera::position` if a `Camera` exists; `VRRig::position` if both `VRInput` and `VRRig` exist and `VRInput::session_running`. If there is no centre, return (nothing is loaded or unloaded).
  3. Unload: for every map entry, if the squared distance from `ChunkC::center()` to the nearest centre exceeds `(view_distance + chunk::length)^2` (chunk::length = 256), `emplace_or_replace<ChunkDestroying>` on the entity and erase it from the map.
  4. Load: `range = ceil(view_distance / chunk::length)`. For each centre, `origin = floor(centre / 256)`; for every `(x, y, z)` in `[-range, range]^3` (x outer, z inner), skip coordinates already in the map and chunks whose centre is farther than `view_distance` from the nearest centre; otherwise create an entity, `emplace<ChunkC>(pos)` and insert `pos -> entity` in the map.
- **finish:** None.

## Expected behavior
- After a tick, every chunk coordinate whose centre is within `view_distance` of some centre is in `ChunkMap` with a `ChunkC` entity; no mapped chunk has its centre farther than `view_distance + 256` from every centre.
- Chunks between `view_distance` and `view_distance + 256` are kept if already loaded and not created if absent (hysteresis).
- An unloaded chunk leaves the map immediately but its entity lives on (tagged `ChunkDestroying`) until TerrainSys destroys it; if the camera comes back first, a second entity is created for the same coordinates.
- Without `Camera` and without a running VR session the system is a no-op; the VR rig position (stage origin), not the head, is the VR centre.
- With the default 768 view distance, range is 3 (a 7^3 candidate block per centre).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [ChunkMap](../components/ChunkMap.md) | host | write (emplace_ctx) | preinit | empty map |
| [ChunkMap](../components/ChunkMap.md) | host | readwrite | tick | erase unloaded, insert new |
| [Settings](../components/Settings.md) | host | read | tick | `find`; `view_distance` |
| [Camera](../components/Camera.md) | host | read | tick | `find`; `position` |
| [VRInput](../components/VRInput.md) | host | read | tick | `find`; `session_running` |
| [VRRig](../components/VRRig.md) | host | read | tick | `find`; `position` |
| [ChunkC](../components/ChunkC.md) | host | readwrite +create | tick | `center()` of mapped chunks; new entities with `ChunkC(pos)` |
| [ChunkDestroying](../components/ChunkDestroying.md) | host | write +create | tick | `emplace_or_replace` on unloaded chunks |

## Side effects
- Creates chunk entities (no destruction: TerrainSys destroys tagged ones).
- `OPTICK_EVENT` profiling marker.

## Ordering and dependencies
- Should run after the systems that move the cameras this frame (`PlayerControl`, `VRControl`, viewer camera systems) and before [ChunkGeneratorSys](ChunkGeneratorSys.md), which picks up new `ChunkC` entities in the same frame. In `base` the order is `PlayerControl`, `VRControl`, `DebugMenu`, `ChunkManager`, `ChunkGenerator`.
- `ChunkMap` must exist before `TerrainColliders` reads it (`find`, optional).
- Main thread only; no worker touches `ChunkMap` or chunk entities.

## Related
- [ChunkGeneratorSys](ChunkGeneratorSys.md), [TerrainSys](TerrainSys.md), [TerrainCollidersSys](TerrainCollidersSys.md)
- [ChunkC](../components/ChunkC.md), [ChunkMap](../components/ChunkMap.md), [ChunkDestroying](../components/ChunkDestroying.md), [Settings](../components/Settings.md)
