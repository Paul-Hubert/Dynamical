# RigidBodySys

> **Kind:** system · **Registry name:** `RigidBody` · **Scope:** Both · **Lane:** Pre (host scenes, e.g. `base`, `vr_roller`) / Fixed (Phase Every) (world templates) · **Needs renderer:** no
> **Source:** [`src/physics/physics_sys.h`](../../../src/physics/physics_sys.h), [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp), [`src/physics/height_field.h`](../../../src/physics/height_field.h), [`src/physics/height_field.cpp`](../../../src/physics/height_field.cpp)

## Purpose
Owns the lifetime of Jolt bodies for three components of its registry: `RigidBody` (needs a `Transform`), `Ground` and `HeightField`. Every tick it creates the bodies of components that do not have one yet (`created()` is false) and, through `on_destroy` signals, removes the body when the component is removed or its entity destroyed. Dynamic bodies also get a `PhysicsPose`. The same class runs on the host (base scene) and in every world; it never steps the simulation.

## Lifecycle
- **Construction / params:** `RigidBodySys(entt::registry&)`; no JSON params (`systemInfo<RigidBodySys>(SystemScope::Both)`).
- **preinit:** `world = &physicsWorld(reg)` (find-or-emplace `PhysicsWorld` with the default `PhysicsConfig`); find-or-emplace `PhysicsStats`; connects `on_destroy<RigidBody>` to `destroyBody`, `on_destroy<Ground>` to `destroyGround`, `on_destroy<HeightField>` to `destroyHeightField`.
- **init:** `createGrounds()`, so static geometry exists before other systems' `init` place bodies on it (world systems run `init` after `World::init` has created the template entities).
- **tick(dt):** `dt` unused.
  1. `createGrounds()`:
     - For each `Ground` not created: static `BoxShape` with half extents `max(size * 0.5, 0.001)`, centred at `(0, top_y - half.y, 0)`, identity rotation, static layer, `mFriction = friction`, `mRestitution = restitution`, `CreateAndAddBody(..., DontActivate)`. Invalid id: warning "out of bodies for the ground", skipped (retried next tick). Else `ground.body` is set.
     - For each `HeightField` not created: if `wave_list` is empty, the seed is `field.seed`, or when that is 0 and a `WorldRng` context variable exists, one `rng->engine()` draw; `drawWaves(field, seed)` fills `wave_list` (`waves` plane waves, wavelength log-uniform in `wavelength`, direction and phase uniform in [0, 2pi), amplitude `wave_slope / k`, drawn from a local `WorldRng(seed)`). `sampleHeights` evaluates `HeightField::height` on a `samples x samples` grid (row-major z then x, spacing `spacing`, centred on the origin). `HeightFieldShapeSettings` with offset `(-half, 0, -half)`, scale `(spacing, 1, spacing)`; error: warning "invalid height field", skipped. Static body at the origin, static layer, friction/restitution from the field.
     - If any ground or field was added: `PhysicsSystem::OptimizeBroadPhase()`.
  2. For each entity with `Transform` and `RigidBody` whose body is not created:
     - Shape: `Sphere` radius `max(half_extent.x, 0.001)`; `Capsule` half-height `max(half_extent.y, 0.001)`, radius `max(half_extent.x, 0.001)`; `Box` half extents `max(half_extent, 0.001)` with convex radius `min(cDefaultConvexRadius, 0.5 * smallest half extent)`. A non-zero `offset` wraps it in a `RotatedTranslatedShape` (translation only).
     - Shape error: warning "invalid shape", entity queued for removal.
     - Motion: `Static` -> `EMotionType::Static` on the static layer; `Kinematic` -> `Kinematic` on the moving layer; `Dynamic` -> `Dynamic` on the moving layer.
     - Body settings: position `transform.position`, rotation `normalize(transform.rotation)` (scale is ignored), `friction`, `restitution`, `angular_damping`, `mAllowSleeping = world->config().allow_sleeping`. Dynamic with `mass > 0`: `mOverrideMassProperties = CalculateInertia` with that mass. Dynamic: initial linear and angular velocity, `EMotionQuality::LinearCast`, `mEnhancedInternalEdgeRemoval = true`.
     - `CreateAndAddBody` with `DontActivate` for static bodies, `Activate` otherwise. Invalid id: warning "out of bodies", queued for removal. Else `body.body = id.GetIndexAndSequenceNumber()`.
     - Dynamic: `emplace_or_replace<PhysicsPose>(entity, position, position, rotation, rotation)` from the Transform.
  3. `reg.remove<RigidBody>` for every queued entity (failures are not retried).
- **destroy callbacks:** `destroyBody` / `destroyGround` / `destroyHeightField` read the component being destroyed; if it has a body (and the cached `world` is set) they `RemoveBody` + `DestroyBody` and reset `body` to the invalid id.
- **finish:** disconnects the three signals, then removes and destroys the bodies of every `HeightField`, `Ground` and `RigidBody` (in that order) and resets their `body` fields; components stay in the registry. The destructor disconnects the signals again and clears `world`.

## Expected behavior
- A `RigidBody` without a `Transform` gets no body until a `Transform` is added.
- A body is created at most once per component: `created()` is checked on every tick.
- Only `Dynamic` bodies get a `PhysicsPose`; kinematic and static bodies never do.
- Static `RigidBody` bodies do not trigger `OptimizeBroadPhase`; grounds and height fields do.
- `RigidBody` components whose shape fails, or that hit `max_bodies`, are removed (one warning each). Grounds and height fields that hit `max_bodies` stay and are retried every tick (warning every tick).
- A `HeightField` with `seed == 0` in a world consumes exactly one 64-bit draw of `WorldRng` when it is created; on the host (no `WorldRng`) seed 0 is used as is. A pre-filled `wave_list` is kept (no draw).
- Removing `RigidBody` (or destroying the entity) removes the Jolt body in the same call. The `PhysicsPose` is NOT removed with the `RigidBody`; on the host `PhysicsSys` would keep writing the frozen pose to the `Transform` (iterates `Transform + PhysicsPose`). In worlds `PhysicsStepSys` requires `RigidBody` so a leftover pose is ignored.
- Replacing a created `RigidBody` with `emplace_or_replace` (scene component loader) fires `on_update`, not `on_destroy`: the new component has an invalid `body`, so a second Jolt body is created and the old one is not destroyed (follows from the code; not covered by a check).
- Bodies are created in view iteration order; together with the sorted terrain cells this keeps world simulation deterministic.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | both | readwrite +emplace_ctx | preinit | find-or-emplace, pointer cached |
| [PhysicsWorld](../components/PhysicsWorld.md) | both | readwrite | init, tick, on_destroy, finish | `CreateAndAddBody`, `RemoveBody`, `DestroyBody`, `OptimizeBroadPhase`, `config().allow_sleeping` |
| [PhysicsStats](../components/PhysicsStats.md) | both | write +emplace_ctx | preinit | find-or-emplace only, no field written |
| [RigidBody](../components/RigidBody.md) | both | readwrite +remove | tick | all creation fields read, `body` written; removed on failure |
| [RigidBody](../components/RigidBody.md) | both | readwrite | on_destroy, finish | `body` read and reset |
| [Transform](../components/Transform.md) | both | read | tick | `position`, `rotation` of new bodies |
| [PhysicsPose](../components/PhysicsPose.md) | both | write +create | tick | emplaced for dynamic bodies |
| [Ground](../components/Ground.md) | both | readwrite | init, tick, on_destroy, finish | `size`, `top_y`, `friction`, `restitution` read; `body` written |
| [HeightField](../components/HeightField.md) | both | readwrite | init, tick, on_destroy, finish | `wave_list` drawn when empty; `samples`, `spacing`, `seed`, `waves`, `wavelength`, `wave_slope`, `friction`, `restitution` read; `body` written |
| [WorldRng](../components/WorldRng.md) | world | readwrite | init, tick | one `engine()` draw per seed-0 `HeightField` without waves |

## Side effects
- Creates, adds, removes and destroys Jolt bodies in the registry's `PhysicsWorld`; calls `OptimizeBroadPhase`.
- Connects / disconnects EnTT `on_destroy` signals for `RigidBody`, `Ground`, `HeightField`.
- Removes `RigidBody` components that failed.
- Consumes `WorldRng` draws (seed-0 height fields).
- Logs warnings (invalid shapes, out of bodies, invalid height field).

## Ordering and dependencies
- Before `PhysicsSys` / `PhysicsStepSys` and `TerrainCollidersSys` / `WorldTerrainSys` in the same lane, so a body added this frame is stepped (and its terrain built) this frame. Shipped scenes list `RigidBody` first in world fixed lanes and before `TerrainColliders`, `Physics` on the host.
- After systems that spawn `RigidBody` entities (e.g. `BoxDrop`, `BoxThrower`, `BoxRing`) when they should get a body the same frame.
- `init` must run before world systems that place agents on the ground in their own `init` (`SystemSet::init` follows list order).
- Main-thread only on the host; in worlds it runs on the world's task (worlds step in parallel, each with its own registry and `PhysicsWorld`). Must not run during a Jolt step.

## Related
[PhysicsSys](PhysicsSys.md), [PhysicsStepSys](PhysicsStepSys.md), [TerrainCollidersSys](TerrainCollidersSys.md), [WorldTerrainSys](WorldTerrainSys.md), [RigidBody](../components/RigidBody.md), [Ground](../components/Ground.md), [HeightField](../components/HeightField.md), [PhysicsPose](../components/PhysicsPose.md), [`src/physics/`](../folders/src-physics.md), [physics.md](../../physical-agents/physics.md)
