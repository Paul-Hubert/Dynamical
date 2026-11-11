# BoxRingSys

> **Kind:** system · **Registry name:** `BoxRing` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/systems/system_list.h`](../../../src/view/systems/system_list.h), [`src/view/systems/base_scene.cpp`](../../../src/view/systems/base_scene.cpp), [`src/view/spawn.cpp`](../../../src/view/spawn.cpp)

## Purpose
Demo content of the `base` scene: at init, drops a ring of 12 dynamic boxes onto the marching cubes terrain around the origin. Does nothing per frame.

## Lifecycle
- **Construction / params:** `DEFINE_SYSTEM(BoxRingSys)`, no JSON params.
- **preinit:** None (empty).
- **init:** `get<std::shared_ptr<TerrainGenerator>>()` (required). For `i` in 0..11: `angle = i * 2 * 3.14159 / 12`, `size = 1 + i % 4`, position `(cos(angle) * 25, surfaceHeight + size + 4, sin(angle) * 25)`, colour `(0.5 + 0.5 cos, 0.5 + 0.5 sin, 0.6, 1)`; `spawnBox(reg, position, size, colour)`; if an entity was created, its `Transform.rotation = angleAxis(angle, normalize(1, 1, 0))`.
- **tick(dt):** None (empty).
- **finish:** None (empty).

## Expected behavior
- 12 host entities with `Transform`, `ModelRenderable`, Dynamic box `RigidBody` (half extent size/2, zero initial velocity) exist after init, unless `resources/models/box.gltf` cannot be loaded (then none).
- Boxes start 4 m plus their size above the analytic surface and fall once physics runs.
- Throws / asserts if no `TerrainGenerator` context variable exists (scene without ChunkGenerator or terrain colliders).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`TerrainGenerator`](../components/TerrainGenerator.md) (`std::shared_ptr<TerrainGenerator>`) | host | read | init | `surfaceHeight(x, z)` |
| [`ModelManager`](../components/ModelManager.md) | host | read | init | spawnBox loads `box.gltf` |
| [`Transform`](../components/Transform.md) | host | write +create | init | spawnBox, then rotation |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | write +create | init | spawnBox |
| [`RigidBody`](../components/RigidBody.md) | host | write +create | init | spawnBox |

## Side effects
- Creates 12 host entities; Jolt bodies are created later by the host physics systems.

## Ordering and dependencies
- The generator must be emplaced in a preinit (ChunkGeneratorSys or the physics terrain colliders), which always precedes every init.
- `ModelManager` comes from the renderer (constructed before the systems).

## Related
[ModelLineupSys](ModelLineupSys.md), [BoxThrowerSys](BoxThrowerSys.md), [folder `src/view/systems/`](../folders/src-view-systems.md), [folder `src/view/`](../folders/src-view.md) (spawnBox).
