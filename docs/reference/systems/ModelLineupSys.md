# ModelLineupSys

> **Kind:** system · **Registry name:** `ModelLineup` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/systems/system_list.h`](../../../src/view/systems/system_list.h), [`src/view/systems/base_scene.cpp`](../../../src/view/systems/base_scene.cpp)

## Purpose
Demo content of the `base` scene: at init, loads every glTF file directly in `resources/models` (not subfolders, except `box.gltf`) and lines them up behind the spawn point, each scaled to 4 units tall with a dynamic box collider around its bounds.

## Lifecycle
- **Construction / params:** `DEFINE_SYSTEM(ModelLineupSys)`, no JSON params.
- **preinit:** None (empty).
- **init:**
  1. `get<ModelManager>()`, `get<std::shared_ptr<TerrainGenerator>>()` (both required).
  2. `x = -20`. For each entry of `directory_iterator("./resources/models")` (errors ignored): skip unless the extension is `.gltf` or `.glb`; skip `box.gltf`; `models.load(path)`, skip on `entt::null`.
  3. `height = max(ModelC.max.y - ModelC.min.y, 0.001)`, `scale = 4 / height`; position `(x, surfaceHeight(x, 15) - min.y * scale + 2, 15)`.
  4. New entity: `Transform(position, identity, vec3(scale))`, `ModelRenderable(model)` (white), `RigidBody` (Dynamic box) with `half_extent = (max - min) / 2 * scale`, `offset = (max + min) / 2 * scale`.
  5. `x += 8`.
- **tick(dt):** None (empty).
- **finish:** None (empty).

## Expected behavior
- One entity per loadable model file; the order along X follows the filesystem's directory iteration order (unspecified, may differ between platforms).
- Each model's lowest point starts 2 m above the terrain surface sampled at its X.
- A missing `resources/models` directory yields no entities and no error.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`ModelManager`](../components/ModelManager.md) | host | read | init | `load()` per file |
| [`TerrainGenerator`](../components/shared_ptr-TerrainGenerator.md) (`std::shared_ptr<TerrainGenerator>`) | host | read | init | `surfaceHeight(x, 15)` |
| [`ModelC`](../components/ModelC.md) | host | read | init | `min`, `max` of the model entity |
| [`Transform`](../components/Transform.md) | host | write +create | init | new entity |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | write +create | init | new entity |
| [`RigidBody`](../components/RigidBody.md) | host | write +create | init | Dynamic box collider |

## Side effects
- Reads the `resources/models` directory and loads glTF files (GPU uploads through ModelManager).
- Creates one host entity per model.

## Ordering and dependencies
- The generator must exist (emplaced in a preinit by ChunkGeneratorSys or the physics terrain colliders).

## Related
[BoxRingSys](BoxRingSys.md), [BoxThrowerSys](BoxThrowerSys.md), [folder `src/view/systems/`](../folders/src-view-systems.md).
