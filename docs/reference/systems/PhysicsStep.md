# PhysicsStep

`PhysicsStepSys` in [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after the systems that set forces and motors this tick, before those that read the poses · Parameters: no

One fixed tick of a world's physics: kinematic bodies moved to their `Transform`, `PhysicsConfig::substeps` Jolt
updates of `h / substeps`, then every dynamic body's `PhysicsPose` and `Transform` set to the new pose (no
interpolation: a world has no frame between ticks; `WorldView` interpolates between the poses to draw).

## Behaviour

- **preinit**: takes the registry's `PhysicsWorld` (the world's own, emplaced by `World` from the template's
  `physics` block; created with the default config when missing).
- **tick(h)** (`h` = `WorldClock::dt`, 1/physics_hz):
  1. Every created kinematic `RigidBody` is moved to its `Transform` (`MoveKinematic` over `h`; no zero-scale
     exception, unlike [Physics](Physics.md)).
  2. `max(1, substeps)` Jolt updates of `h / substeps` (zone `Jolt update`).
  3. Every entity with `Transform` + `RigidBody` (created) + `PhysicsPose`: previous pose = current, current = Jolt's
     pose, `Transform` = current.
- It calls no `FixedSteps` hooks (world systems are themselves in the fixed list) and destroys nothing.

## Parameters

None. `substeps` comes from the world template's `physics` block (`PhysicsConfig`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit (`physicsWorld`), tick | Jolt updates, kinematic moves, pose reads, `config().substeps` |
| [Transform](../components/Transform.md) | component | readwrite | tick | kinematic targets read; dynamic bodies' pose written |
| [RigidBody](../components/RigidBody.md) | component | read | tick | motion and body id |
| [PhysicsPose](../components/PhysicsPose.md) | component | readwrite | tick | previous / current pose shifted and updated |

## Side effects

- Steps the world's Jolt `PhysicsSystem` (single threaded with `threads: 1`, the worlds then run in parallel on the
  executor of `Worlds::step`).

## Order and dependencies

- After [RigidBody](RigidBody.md) and [WorldTerrain](WorldTerrain.md) (bodies and terrain colliders exist), after
  the systems that apply actions, drives and forces this tick (`TrainerActions@act`, `DriveTargets@act`,
  `AnimalDrive@act`, `SphereControl`, `RigPoseApply`...).
- Before the systems that read the new poses (`BodySync`, `AnimalSync`, tasks, rewards, observations `@observe`).

## Used in scenes
<!-- generated -->

## Tests

- `S2.1`, `S2.2`, `S2.3` ([`tests/unit/test_boxes.cpp`](../../../tests/unit/test_boxes.cpp)): boxes rest on the ground after 600 ticks, determinism with 1 and 8 threads, isolation of worlds (`World::hash`).
- `S2.4 friction combine` ([`tests/unit/friction_combine.cpp`](../../../tests/unit/friction_combine.cpp)).
- `Worlds: clock and phases and physics and seeds from the template` ([`tests/unit/world.cpp`](../../../tests/unit/world.cpp)).
- Every world-based suite (`SR.*`, `IK.*`, `GAIT.*`, `TW.*`, `AA.*`, `SANDBOX.*`) steps through it.
- `V2.1 test_boxes_view at frame 300 matches its golden` ([`tests/capture/worlds_boxes.capture.json`](../../../tests/capture/worlds_boxes.capture.json)).
