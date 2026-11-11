# PhysicsStepSys

> **Kind:** system · **Registry name:** `PhysicsStep` · **Scope:** World · **Lane:** Fixed (Phase Every) · **Needs renderer:** no
> **Source:** [`src/physics/physics_sys.h`](../../../src/physics/physics_sys.h), [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp)

## Purpose
One fixed tick of a world's physics: moves kinematic bodies to their `Transform`, runs `PhysicsConfig::substeps` Jolt updates of `h / substeps`, then copies the new pose of every dynamic body into its `PhysicsPose` (shifting the previous one) and its `Transform`. No accumulator: the world's `SystemSet::fixed_tick` calls it once per tick with `h = WorldClock::dt`.

## Lifecycle
- **Construction / params:** `PhysicsStepSys(entt::registry&)`; no JSON params. Sub-stepping comes from the template's `"physics": { "substeps" }`.
- **preinit:** `world = &physicsWorld(reg)` (the `World` constructor has already emplaced it from the template config).
- **init:** none.
- **tick(h):**
  1. `substeps = max(1, config().substeps)`, `step = h / substeps`.
  2. Every `Transform + RigidBody` with `motion == Kinematic` and `created()`: `MoveKinematic(body, position, normalize(rotation), h)` (no scale check, unlike `PhysicsSys`).
  3. `substeps` times `world->step(step)`.
  4. Every `Transform + RigidBody + PhysicsPose` with `created()`: `prev_* = current`; current = Jolt pose; `transform.position/rotation = current`.
- **finish:** none.

## Expected behavior
- The world's simulation advances exactly `h` per tick, independent of wall time and frame rate.
- Dynamic Transforms hold the exact post-step pose (not interpolated); viewers interpolate with `PhysicsPose` themselves.
- Kinematic targets are reached over the whole tick (`MoveKinematic` duration `h`) even with several substeps.
- Bodies created this tick by an earlier `RigidBodySys` are stepped; their `PhysicsPose` starts as the spawn pose for both prev and current.
- No fallen-body removal and no stats (unlike `PhysicsSys`).
- Given the same initial state and inputs, identical across runs with `threads: 1` (deterministic body order, single-threaded job system); `World::hash` checks this.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | world | readwrite +emplace_ctx | preinit | find (emplaces only if missing) |
| [PhysicsWorld](../components/PhysicsWorld.md) | world | readwrite | tick | `config().substeps`, `MoveKinematic`, `step`, `GetPositionAndRotation` |
| [RigidBody](../components/RigidBody.md) | world | read | tick | `motion`, `body` |
| [Transform](../components/Transform.md) | world | readwrite | tick | kinematic targets read; dynamic `position`, `rotation` written |
| [PhysicsPose](../components/PhysicsPose.md) | world | readwrite | tick | shifted and refreshed |

## Side effects
- Advances the world's Jolt simulation (`JobSystemSingleThreaded` in shipped templates, `threads: 1`).

## Ordering and dependencies
- After `RigidBodySys`, `WorldTerrainSys` and the systems that apply this tick's forces / kinematic targets (`SphereControl` etc.); before the task and observation systems that read post-step state (`SphereTask`, `SphereObserve@observe`). Shipped: `RigidBody`, `WorldTerrain`, `TrainerActions@act`, `SphereControl`, `PhysicsStep`, `SphereTask`, `SphereObserve@observe`.
- Runs on the world's task; worlds run in parallel on taskflow (`Worlds::step`), each with its own registry and `PhysicsWorld`.

## Related
[PhysicsSys](PhysicsSys.md), [RigidBodySys](RigidBodySys.md), [WorldTerrainSys](WorldTerrainSys.md), [PhysicsWorld](../components/PhysicsWorld.md), [PhysicsPose](../components/PhysicsPose.md), [`src/physics/`](../folders/src-physics.md), [simulation.md](../../physical-agents/simulation.md)
