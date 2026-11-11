# BoxDropSys

> **Kind:** system · **Registry name:** `BoxDrop` · **Scope:** World · **Lane:** Fixed (Phase Every; the tick is empty, the work happens in init) · **Needs renderer:** no
> **Source:** [`src/logic/systems/box_drop.h`](../../../src/logic/systems/box_drop.h), [`src/logic/systems/box_drop.cpp`](../../../src/logic/systems/box_drop.cpp)

## Purpose
Scene setup for the physics test worlds (`box_world` template in `resources/scenes/common/box_world.json`, used by `test_boxes` and `test_boxes_view`): in `init` it creates `count` dynamic bodies on a centred, jittered grid above y = 0, with sizes, heights, orientations and colours drawn from the world's `WorldRng`. Every `capsule_every`-th body is a capsule, the others are cubes. It does nothing afterwards.

## Lifecycle
- **Construction / params:** `BoxDropSys(reg, params)`, registered with a params factory in `src/scene/system_registry.cpp`. JSON params (all optional, no validation beyond nlohmann's type conversion):
  - `count` (int, default 50): number of bodies.
  - `spacing` (float, default 1.4): grid pitch, world units.
  - `height` ([float, float], default [0.5, 2]): range of the drop height of the body's bottom; read as `params["height"][0]`, `[1]`.
  - `half_extent` ([float, float], default [0.15, 0.3]): range of the half size `half`.
  - `capsule_every` (int, default 0): 0 = no capsules.
  The scene `box_world` passes `count: ${boxes}` (50), `spacing: 2.0`, `height: [0.3, 1.0]`, `half_extent: [0.15, 0.3]`, `capsule_every: ${capsule_every}` (5).
- **preinit:** None (base class no-op).
- **init:**
  1. `cols = max(1, ceil(sqrt(count)))`, `rows = ceil(count / cols)`.
  2. For each `i` in `[0, count)`: draw `half = uniform(half_extent.x, half_extent.y)`; `jitter = 0.2 * spacing`; position x = `((i % cols) - (cols - 1) / 2) * spacing + uniform(-jitter, jitter)`, y = `uniform(height.x, height.y) + half`, z = `((i / cols) - (rows - 1) / 2) * spacing + uniform(-jitter, jitter)`; rotation = `angleAxis(uniform(0, 2 pi), rng.unitSphere())`.
  3. Create an entity with `Transform(position, rotation)` and a default `RigidBody` (dynamic, box) with `friction = 1`, `restitution = 0`.
  4. If `capsule_every > 0 && i % capsule_every == capsule_every - 1`: `shape = Capsule`, `half_extent = (0.6 half, 1.5 half, 0.6 half)`, `angular_damping = 1` (Jolt has no rolling resistance), `ModelRef("models/primitives/capsule", (0.95, 0.75, 0.25, 1))`.
     Otherwise: `half_extent = (half, half, half)`, `ModelRef("models/primitives/box.gltf", (0.3 + 0.5 u, 0.45, 0.85, 1))` with one more uniform draw `u`.
- **tick(dt):** empty.
- **finish:** None.

## Expected behavior
- After `World::init`, the world has exactly `count` new entities with `Transform`, `RigidBody` and `ModelRef`; the Jolt bodies appear on the first tick of `RigidBodySys`.
- With `capsule_every = 5`, bodies 4, 9, 14, ... are capsules (`Shape::Capsule`, angular damping 1); the rest are cubes.
- Every body's bottom (for cubes, ignoring rotation) starts between `height.x` and `height.y` above y = 0; the grid is centred on the origin in XZ with up to +-20% of `spacing` jitter.
- The layout depends only on the world seed (`worldSeed(base_seed, index)`): same seed, same bodies, on the same compiler. Note that the three draws inside the `glm::vec3` position constructor and the two inside `angleAxis(...)` are function arguments, whose evaluation order C++ leaves unspecified, so the exact assignment of draws to axes may differ between compilers.
- `count <= 0` creates nothing.
- The capsule `ModelRef.path` (`models/primitives/capsule`, no extension) has no file under `resources/`; it is harmless today because nothing resolves `ModelRef.path` (WorldView only uses `ModelRef.color` and builds capsule meshes itself).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [WorldRng](../components/WorldRng.md) | world | readwrite | init | `get`; per body: half, x jitter, height, z jitter, angle, unitSphere (2 draws), plus colour for cubes |
| [Transform](../components/Transform.md) | world | write +create | init | position on the jittered grid, random rotation, scale 1 |
| [RigidBody](../components/RigidBody.md) | world | write +create | init | dynamic box or capsule, friction 1, restitution 0, capsule angular_damping 1 |
| [ModelRef](../components/ModelRef.md) | world | write +create | init | path and colour per shape |

## Side effects
- Creates `count` entities in the world registry.
- Consumes 7 (capsule) or 8 (cube) `WorldRng` uniform draws per body, which shifts every later draw of the world stream.
- No Jolt calls itself: `RigidBodySys` creates the bodies on its first tick.

## Ordering and dependencies
- Needs `WorldRng` (emplaced by the `World` constructor).
- `World::init` creates the template entities (the ground) before `SystemSet::init`, so the ground exists; the bodies are created by `RigidBodySys::tick`, which must be in the fixed lane (`box_world` lists `BoxDrop`, `RigidBody`, `PhysicsStep`).
- Runs once, single-threaded, inside world init.

## Related
- [RigidBodySys](RigidBodySys.md), [PhysicsStepSys](PhysicsStepSys.md), [WorldViewSys](WorldViewSys.md)
- [Transform](../components/Transform.md), [RigidBody](../components/RigidBody.md), [ModelRef](../components/ModelRef.md), [Ground](../components/Ground.md)
- [`docs/physical-agents/physics.md`](../../physical-agents/physics.md), [`resources/scenes/common/box_world.json`](../../../resources/scenes/common/box_world.json)
