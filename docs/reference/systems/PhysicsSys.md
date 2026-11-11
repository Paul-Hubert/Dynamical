# PhysicsSys

> **Kind:** system · **Registry name:** `Physics` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** no
> **Source:** [`src/physics/physics_sys.h`](../../../src/physics/physics_sys.h), [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp)

## Purpose
The host (base scene) Jolt step. Converts variable frame time into fixed 60 Hz steps through `FixedStepClock` (at most 4 per frame), drives kinematic bodies to their `Transform` before each step, records the last two poses of dynamic bodies in `PhysicsPose`, writes their `Transform` interpolated between those poses, and destroys entities that fell below `kill_height`.

## Lifecycle
- **Construction / params:** `PhysicsSys(entt::registry&)`; no JSON params. `kill_height = -1000.f`.
- **preinit:** `world = &physicsWorld(reg)`; find-or-emplace `FixedStepClock` and `PhysicsStats`.
- **init:** none.
- **tick(dt):**
  1. `steps = clock.stepsFor(dt)` = `min(floor((accumulator + min(dt, 0.25)) / (1/60)), 4)`; `accumulator += min(dt, 0.25)`; `stats.steps_last_frame = steps`.
  2. If `steps > 0`, `steps` times:
     - every `Transform + RigidBody` with `motion == Kinematic`, `created()` and `transform.scale.x > 0`: `MoveKinematic(body, position, normalize(rotation), fixed_dt)`;
     - `world->step(fixed_dt)` (one Jolt `Update`, 1 collision step); `clock.sim_time += fixed_dt`;
     - every `RigidBody + PhysicsPose` with `created()`: `prev_* = current`, current = Jolt `GetPositionAndRotation`.
     Then `stats.step_ms` = wall time of the loop (0 when `RunArgs::deterministic()`), `accumulator -= steps * fixed_dt`, and if `accumulator > fixed_dt` it is reset to 0 (spiral guard).
  3. `alpha = accumulator / fixed_dt`. Every `Transform + PhysicsPose`: `position = mix(prev_position, position, alpha)`, `rotation = slerp(prev_rotation, rotation, alpha)`; entities with interpolated `position.y < -1000` are collected and `reg.destroy`ed.
  4. `stats.bodies = GetNumBodies()`, `stats.active_bodies = GetNumActiveBodies(RigidBody)`.
- **finish:** none (bodies are removed by `RigidBodySys::finish`).

## Expected behavior
- At 60 fps exactly one step per frame on average; frames longer than 0.25 s count as 0.25 s; never more than 4 steps per frame, excess time is dropped (accumulator reset to 0, alpha 0).
- Rendered dynamic Transforms lag the simulation by up to one step (interpolation between the last two steps).
- Kinematic bodies with `scale.x <= 0` (hidden VR hands) are not moved. The same kinematic target is used for every step of the frame.
- Kinematic and static Transforms are never written; dynamic Transforms are overwritten each frame (gameplay writes to them are lost).
- An entity with `PhysicsPose` but no `RigidBody` is still interpolated (frozen) every frame.
- Falling below y = -1000 destroys the whole entity (and through `RigidBodySys::destroyBody`, its Jolt body).
- `step_ms` keeps the value of the last frame that stepped; `steps_last_frame`, `bodies` and `active_bodies` are refreshed every frame.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | host | readwrite +emplace_ctx | preinit | find-or-emplace |
| [PhysicsWorld](../components/PhysicsWorld.md) | host | readwrite | tick | `MoveKinematic`, `step`, `GetPositionAndRotation`, body counts |
| [FixedStepClock](../components/FixedStepClock.md) | host | write +emplace_ctx | preinit | find-or-emplace |
| [FixedStepClock](../components/FixedStepClock.md) | host | readwrite | tick | `accumulator`, `sim_time` |
| [PhysicsStats](../components/PhysicsStats.md) | host | write +emplace_ctx | preinit | find-or-emplace |
| [PhysicsStats](../components/PhysicsStats.md) | host | write | tick | `steps_last_frame`, `step_ms`, `bodies`, `active_bodies` |
| [RunArgs](../components/RunArgs.md) | host | read | tick | `deterministic()` |
| [RigidBody](../components/RigidBody.md) | host | read | tick | `motion`, `body` |
| [Transform](../components/Transform.md) | host | readwrite +destroy | tick | kinematic targets read (`position`, `rotation`, `scale.x`); interpolated `position`, `rotation` written; fallen entities destroyed |
| [PhysicsPose](../components/PhysicsPose.md) | host | readwrite +destroy | tick | shifted and refreshed per step; read for interpolation |

## Side effects
- Advances the host Jolt simulation (thread-pool job system unless configured otherwise).
- Destroys entities that fell below `kill_height` (fires `on_destroy` for all their components).
- Wall-clock timing for `step_ms`.

## Ordering and dependencies
- After the gameplay systems that write kinematic Transforms (`VRControl` hands) and spawn bodies, after `RigidBodySys` and `TerrainCollidersSys` (which must see the accumulator before this system adds `dt`). Shipped order ends with `RigidBody`, `TerrainColliders`, `Physics`.
- Before anything that reads dynamic Transforms for rendering (post lane, renderer).
- Main thread. Jolt queries with the NoLock interfaces (`PhysicsWorld::groundBelow` etc.) must not run concurrently with this tick.

## Related
[RigidBodySys](RigidBodySys.md), [TerrainCollidersSys](TerrainCollidersSys.md), [PhysicsStepSys](PhysicsStepSys.md), [FixedStepClock](../components/FixedStepClock.md), [PhysicsPose](../components/PhysicsPose.md), [`src/physics/`](../folders/src-physics.md), [physics.md](../../physical-agents/physics.md)
