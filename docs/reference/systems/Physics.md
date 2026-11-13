# Physics

`PhysicsSys` in [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, last of the physics systems (`RigidBody`, `TerrainColliders`, `Physics`) · Parameters: no

The host's Jolt step: a fixed 60 Hz accumulator over the frame time, at most 4 steps a frame. Kinematic bodies
follow their `Transform`, the host systems' `FixedSteps` run around each step, dynamic bodies' `Transform`s are
interpolated between the last two simulated poses, and bodies that fell under -1000 m are destroyed with their
entity.

## Behaviour

- **preinit**: takes the registry's `PhysicsWorld` (created with the default `PhysicsConfig` when missing: a Jolt
  thread pool of `hardware_concurrency / 2 - 1` threads), and makes sure `FixedStepClock` and `PhysicsStats` exist.
- **tick(dt)**:
  1. `steps = FixedStepClock::stepsFor(dt)` = `min((accumulator + min(dt, 0.25)) / (1/60), 4)`; the accumulator
     grows by `min(dt, 0.25)`. `PhysicsStats::steps_last_frame` = steps.
  2. For each step:
     - every created **kinematic** `RigidBody` whose `Transform::scale.x > 0` is moved to its `Transform`
       (`MoveKinematic` over 1/60 s); hidden ones (zero scale) stay put;
     - `FixedSteps::before` functions are called with 1/60 (in registration order);
     - one Jolt update (`PhysicsWorld::step(1/60)`), `FixedStepClock::sim_time += 1/60`;
     - every created body with a `PhysicsPose` shifts `position`/`rotation` into `prev_*` and reads the new pose
       from Jolt;
     - `FixedSteps::after` functions are called.
  3. `PhysicsStats::step_ms` (0 in deterministic runs); accumulator minus the steps taken, reset to 0 when more than
     one step is still left (too slow to keep up: the simulation falls behind instead of spiralling).
  4. Every entity with `Transform` + `PhysicsPose` gets `position = mix(prev, current, alpha)`, `rotation =
     slerp(...)`, `alpha = accumulator / (1/60)`. Those under `kill_height` (-1000 m) are destroyed (the on_destroy
     handler of [RigidBody](RigidBody.md) removes their body).
  5. `PhysicsStats::bodies`, `active_bodies`.

## Parameters

None. Constants: `FixedStepClock::fixed_dt` 1/60 s, `max_steps` 4, frame time clamped to 0.25 s,
`PhysicsSys::kill_height` -1000.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit (`physicsWorld`), tick | Jolt update, kinematic moves, pose reads, body counts |
| [FixedStepClock](../components/FixedStepClock.md) | context | readwrite+create | preinit, tick | accumulator and `sim_time` advanced |
| [PhysicsStats](../components/PhysicsStats.md) | context | readwrite+create | preinit, tick | `steps_last_frame`, `step_ms`, `bodies`, `active_bodies` written |
| [FixedSteps](../components/FixedSteps.md) | context | read | tick | `before` / `after` hooks called around each step |
| [RunArgs](../components/RunArgs.md) | context | read | tick (`deterministic`) | timings reported as 0 in fixed-clock runs |
| [Transform](../components/Transform.md) | component | readwrite+destroy | tick | kinematic targets read; interpolated pose written for dynamic bodies; fallen entities destroyed |
| [RigidBody](../components/RigidBody.md) | component | read+destroy | tick | motion, body id; destroyed with fallen entities |
| [PhysicsPose](../components/PhysicsPose.md) | component | readwrite+destroy | tick | last two simulated poses shifted and read back; destroyed with fallen entities |

## Side effects

- Steps the host's Jolt `PhysicsSystem` (on its job system threads).
- Destroys entities (the whole entity) that fell under -1000 m.
- Runs the `FixedSteps` functions of other systems (players' legs, hands, riders) inside its tick.
- Profiler: zones `Physics step`, `Fixed steps before`, `Jolt update`, `Fixed steps after`; plots `Physics steps`,
  `Active bodies`.

## Order and dependencies

- After [RigidBody](RigidBody.md) and [TerrainColliders](TerrainColliders.md) (bodies exist and their colliders
  are built for exactly the steps this tick takes).
- After the gameplay systems that write kinematic `Transform`s (`VRControl`, hands) and register `FixedSteps`
  (in their preinit).
- Before the systems that read the interpolated `Transform`s (`DesktopView`, which runs after `Physics`, the
  renderer).

## Used in scenes
<!-- generated -->

## Tests

- `T2.4 base system list after the PhysicsSys split` ([`tests/unit/base_scene.cpp`](../../../tests/unit/base_scene.cpp)).
- `PLAYER.1` to `PLAYER.9` ([`tests/unit/player.cpp`](../../../tests/unit/player.cpp)), `MOUNT.*` ([`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp)): `RigidBodySys` + `PhysicsSys` built directly, `FixedSteps`.
- Base probe `S0.1`, `S1.1` ([`tests/golden/base_probe.csv`](../../../tests/golden/base_probe.csv)).
