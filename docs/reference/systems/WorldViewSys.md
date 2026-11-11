# WorldViewSys

> **Kind:** system · **Registry name:** `WorldView` · **Scope:** Host · **Lane:** Post · **Needs renderer:** yes
> **Source:** [`src/view/world_view.h`](../../../src/view/world_view.h), [`src/view/world_view.cpp`](../../../src/view/world_view.cpp)

## Purpose
Draws the bodies of the scene's worlds. The renderer only reads the host registry, so for every created `Ground` and every `Transform` + created `RigidBody` of each (selected) world, WorldView keeps one host "mirror" entity with `Transform`, [`ModelRenderable`](../components/ModelRenderable.md) and [`WorldViewLink`](../components/WorldViewLink.md), and rewrites its pose every frame. World `i` is offset on an XZ grid by `gridOffset(i, worldCount, spacing)`. Boxes and spheres scale the unit primitives of `resources/models/primitives`; capsules get a generated mesh per (radius, half-height). Dynamic bodies are drawn between their last two ticks with the WorldStepper alpha.

## Lifecycle
- **Construction / params:** `WorldViewSys(reg, params)`:
  - `spacing` (float, 6.0): grid spacing in metres (`params.value`).
  - `worlds` (array of uint, optional): indices to draw; any non-array value (the scenes pass `"all"`) means every world.
  - `colors` (array of `[r, g, b]` or `[r, g, b, a]`, optional): world `i`'s bodies take `colors[i % n]`, overriding their own colour.
  - Wrong element types throw a nlohmann exception; unknown keys are ignored.
- **preinit / init:** None (default).
- **tick(dt):**
  1. Return if `Worlds` or `ModelManager` is missing. `alpha` from `WorldStepperState` (1 when absent).
  2. For each world index (skipping those not in `worlds` when given): `offset = gridOffset(index, worlds->size(), spacing)`.
     - Each `Ground` with `created()`: box primitive, colour (0.55, 0.57, 0.5), position `(0, top_y - size.y / 2, 0)`, identity rotation, scale `size`.
     - Each `Transform` + `RigidBody` with `created()`: pose from `Transform`, or `PhysicsPose` lerp / slerp with `alpha`; `position += rotation * body.offset`; colour = hash of the entity id (0.35..0.95 per channel), overridden by `ModelRef::color`, overridden by `colors`. Model: Box -> `primitives/box.gltf`, scale `2 * half_extent`; Sphere -> `primitives/sphere.gltf`, scale `2 * half_extent.x`; Capsule -> `ModelManager::add("capsule:<r>:<h>", capsuleMesh(r, h))`, scale 1. Skipped when the model is `entt::null`.
     - `place`: key `(world index, source entity)` recorded as seen; on first sight creates a host entity with `Transform`, `ModelRenderable(model, color)`, `WorldViewLink(index, source)`; then writes `position = offset + position`, `rotation`, `scale`.
  3. Every link whose key was not seen this tick: destroy the host entity (if valid) and drop the link.
- **finish:** destroys every mirror entity still valid and clears the links.

## Expected behavior
- One mirror entity per (world, ground) and per (world, created body); bodies not yet created in Jolt are not drawn.
- `gridOffset`: `cols = max(1, ceil(sqrt(count)))`, `rows = ceil(count / cols)`, row-major, centred on the origin; `spacing = 0` stacks all worlds at the origin (sphere/vr_roller scenes). The grid always uses the total world count, also when `worlds` filters.
- Model and colour are set only when the mirror is created; later changes of shape, extents-derived model (capsules) or colour are not reflected, while scale and pose are updated each tick.
- Removing a body (or its model failing to load) destroys its mirror the same tick.
- A world entity carrying both `Ground` and `Transform` + `RigidBody` would map to the same key and be placed twice (last write wins); not expected in practice.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Worlds`](../components/Worlds.md) | host | read | tick | every world |
| [`ModelManager`](../components/ModelManager.md) | host | readwrite | tick | `load` primitives, `add` capsule meshes |
| [`WorldStepperState`](../components/WorldStepperState.md) | host | read | tick | `alpha` |
| [`Ground`](../components/Ground.md) | world | read | tick | `size`, `top_y`, `created()` |
| [`Transform`](../components/Transform.md) | world | read | tick | body pose fallback |
| [`RigidBody`](../components/RigidBody.md) | world | read | tick | `created()`, `shape`, `half_extent`, `offset` |
| [`PhysicsPose`](../components/PhysicsPose.md) | world | read | tick | interpolated pose |
| [`ModelRef`](../components/ModelRef.md) | world | read | tick | `color` (the `path` is ignored) |
| [`Transform`](../components/Transform.md) | host | write +create +destroy | tick | mirror pose every tick |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | write +create +destroy | tick | set once at creation |
| [`WorldViewLink`](../components/WorldViewLink.md) | host | write +create +destroy | tick | set once at creation |
| [`WorldViewLink`](../components/WorldViewLink.md) | host | write +destroy | finish | all mirrors destroyed |

## Side effects
- Creates and destroys host entities.
- `ModelManager::add` builds and uploads a capsule mesh the first time a (radius, half-height) pair is seen; `load` loads the primitives once.

## Ordering and dependencies
- After [WorldStepperSys](WorldStepperSys.md) (poses and alpha of this frame). The shipped scenes put it in the post lane, before `Terrain`, i.e. after every pre system and before `renderer->render()`.
- The renderer's ModelPass reads the mirrors during `render()`.

## Related
[WorldStepperSys](WorldStepperSys.md), [SphereViewSys](SphereViewSys.md), [`WorldViewLink`](../components/WorldViewLink.md), [`docs/physical-agents/scenes.md`](../../physical-agents/scenes.md), [folder `src/view/`](../folders/src-view.md).
