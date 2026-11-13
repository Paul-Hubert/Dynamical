# RigidBody

`RigidBodySys` in [`src/physics/physics_sys.cpp`](../../../src/physics/physics_sys.cpp) · Scope: Both · Needs renderer: no
· Lanes: `host.pre` (before `TerrainColliders`, `Physics`), a world's `fixed` list (at the start, before `WorldTerrain` and `PhysicsStep`) · Parameters: no

Turns the physics components of the registry into Jolt bodies of the registry's `PhysicsWorld`: a body for every
new `RigidBody` (next to a `Transform`), a static box for every `Ground`, a static height field for every
`HeightField`. Removes the Jolt body when the component goes (entity destroyed or component removed). It does not
step anything: [Physics](Physics.md) (host) or [PhysicsStep](PhysicsStep.md) (worlds) do.

## Behaviour

- **preinit**: takes the registry's `PhysicsWorld` (`physicsWorld(reg)`: emplaced with the default `PhysicsConfig`
  when the registry has none, i.e. on the host; a world's comes from its template), makes sure `PhysicsStats`
  exists, and connects `on_destroy` handlers for `RigidBody`, `Ground` and `HeightField`.
- **init**: `createGrounds()`, so static geometry exists before the first tick (world systems place their bodies on
  it in init).
- **tick**:
  1. `createGrounds()` again (grounds and height fields added later).
  2. For every `Transform` + `RigidBody` not yet `created()`:
     - shape: `Sphere` (radius `half_extent.x`), `Capsule` (half height `half_extent.y`, radius `half_extent.x`,
       along local Y) or `Box` (`half_extent`, convex radius at most half the smallest extent), all at least
       0.001; wrapped in a `RotatedTranslatedShape` when `offset` is not zero;
     - motion and layer: `Static` → static, `PhysicsLayers::static_layer`; `Dynamic` / `Kinematic` →
       `PhysicsLayers::moving_layer`;
     - at the `Transform`'s position and normalised rotation, with `friction`, `restitution`, `angular_damping`,
       `allow_sleeping` from the world's config, user data = entity + 1 (`bodyEntity` reads it back);
     - dynamic bodies only: `lock_rotation` keeps translations only, `mass > 0` overrides the mass (inertia
       recomputed), `initial_velocity` / `initial_angular_velocity`, `LinearCast` motion quality (no tunnelling
       through thin terrain triangles) and enhanced internal edge removal;
     - created and added (activated unless static); the body id goes to `RigidBody::body`;
     - dynamic bodies get a `PhysicsPose` (both poses = the `Transform`), which [Physics](Physics.md) /
       [PhysicsStep](PhysicsStep.md) update and interpolate.
  3. Bodies that failed (invalid shape, out of bodies: logged) have their `RigidBody` removed so they are not
     retried every frame.
- `createGrounds()`: every `Ground` not created becomes a static box of `size` whose top face is at `top_y`,
  centred on x = z = 0; every `HeightField` not created draws its waves when `wave_list` is empty (seed `seed`, or
  one draw of the `WorldRng` engine when 0), samples its heights and becomes a static Jolt `HeightFieldShape`
  centred on the origin. When anything was added, `OptimizeBroadPhase()`.
- **Signal handlers** `destroyBody`, `destroyGround`, `destroyHeightField` (on_destroy): remove and destroy the
  Jolt body and reset the id.
- **finish**: disconnects the handlers and removes every height field, ground and rigid body body from Jolt,
  resetting their ids (components stay). The destructor disconnects again.

## Parameters

None. The body is described by the components (`RigidBody`, `Ground`, `HeightField` JSON fields, see their
pages) and the world's `PhysicsConfig` (`allow_sleeping`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit (`physicsWorld`), init, tick, finish, handlers | created with the default config when missing; bodies created, added, removed, destroyed |
| [PhysicsStats](../components/PhysicsStats.md) | context | write+create | preinit | emplaced when missing (not written here) |
| [Transform](../components/Transform.md) | component | read | tick | initial pose of a new body |
| [RigidBody](../components/RigidBody.md) | component | readwrite+destroy | tick, finish, `destroyBody` (on_destroy) | read for the body settings, `body` id written; removed from entities whose body failed |
| [PhysicsPose](../components/PhysicsPose.md) | component | write+create | tick | put on new dynamic bodies, both poses at the Transform |
| [Ground](../components/Ground.md) | component | readwrite | init, tick (`createGrounds`), finish, `destroyGround` | static box made, `body` written / reset |
| [HeightField](../components/HeightField.md) | component | readwrite | init, tick (`createGrounds`), finish, `destroyHeightField` | waves drawn into `wave_list`, static height field made, `body` written / reset |
| [WorldRng](../components/WorldRng.md) | context | readwrite | init, tick (`createGrounds`) | one draw of the engine for a `HeightField` whose seed is 0 |

## Side effects

- Creates, adds, removes and destroys Jolt bodies in the registry's `PhysicsWorld`; `OptimizeBroadPhase()` after
  static geometry is added.
- Connects `on_destroy<RigidBody>`, `on_destroy<Ground>`, `on_destroy<HeightField>` (disconnected in finish and in
  the destructor).
- Log warnings: `Physics : invalid shape, ...`, `Physics : out of bodies`, `... for the ground`, `... for the height
  field`, `Physics : invalid height field, ...`.

## Order and dependencies

- Host: before [TerrainColliders](TerrainColliders.md) and [Physics](Physics.md) (bodies made this frame are
  simulated and get colliders this frame), after the systems that spawn `RigidBody` entities (`BoxThrower`,
  `BoxRing`, players).
- Worlds: at the start of the `fixed` list (after `BoxDrop` in box_world, before `WorldTerrain`, the agent factories, `PhysicsStep`); a world's
  `PhysicsWorld` is emplaced by `World` from the template's `physics` block before any preinit.
- In init it runs in scene order: world systems listed after it find the grounds already in Jolt.

## Used in scenes

<!-- generated -->
- [`base.scene.json`](../../../resources/scenes/base.scene.json): `host.pre`
- [`common/animal_demo_terrain_world.json`](../../../resources/scenes/common/animal_demo_terrain_world.json): `fixed`
- [`common/animal_demo_world.json`](../../../resources/scenes/common/animal_demo_world.json): `fixed`
- [`common/animal_orders_world.json`](../../../resources/scenes/common/animal_orders_world.json): `fixed`
- [`common/animal_terrain_world.json`](../../../resources/scenes/common/animal_terrain_world.json): `fixed`
- [`common/animal_world.json`](../../../resources/scenes/common/animal_world.json): `fixed`
- [`common/box_world.json`](../../../resources/scenes/common/box_world.json): `fixed`
- [`common/humanbot_terrain_world.json`](../../../resources/scenes/common/humanbot_terrain_world.json): `fixed`
- [`common/humanbot_world.json`](../../../resources/scenes/common/humanbot_world.json): `fixed`
- [`common/ik_demo_world.json`](../../../resources/scenes/common/ik_demo_world.json): `fixed`
- [`common/ik_world.json`](../../../resources/scenes/common/ik_world.json): `fixed`
- [`common/rig_world.json`](../../../resources/scenes/common/rig_world.json): `fixed`
- [`common/sandbox_world.json`](../../../resources/scenes/common/sandbox_world.json): `fixed`
- [`common/sphere_world.json`](../../../resources/scenes/common/sphere_world.json): `fixed`
- [`common/sphere_world_hills.json`](../../../resources/scenes/common/sphere_world_hills.json): `fixed`
- [`forest.scene.json`](../../../resources/scenes/forest.scene.json): `host.pre`
- [`forest_herds.scene.json`](../../../resources/scenes/forest_herds.scene.json): `host.pre`
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.pre`
- [`player_fps.scene.json`](../../../resources/scenes/player_fps.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`
- [`player_vr.scene.json`](../../../resources/scenes/player_vr.scene.json): `host.pre`
- [`vegetation_test.scene.json`](../../../resources/scenes/vegetation_test.scene.json): `host.pre`
- [`vr_roller.scene.json`](../../../resources/scenes/vr_roller.scene.json): `host.pre`

## Tests

- `S2.1`, `S2.2`, `S2.3` ([`tests/unit/test_boxes.cpp`](../../../tests/unit/test_boxes.cpp)): boxes and capsules dropped on a `Ground`, determinism with 1 and 8 threads, world isolation.
- `S2.4 friction combine` ([`tests/unit/friction_combine.cpp`](../../../tests/unit/friction_combine.cpp)).
- `Worlds: clock and phases and physics and seeds from the template` ([`tests/unit/world.cpp`](../../../tests/unit/world.cpp)): fixed list order.
- `PLAYER.*` ([`tests/unit/player.cpp`](../../../tests/unit/player.cpp)), `MOUNT.*` ([`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp)): constructed directly with `PhysicsSys`.
- `SR.*` ([`tests/unit/sphere_roller.cpp`](../../../tests/unit/sphere_roller.cpp)): `HeightField` and spheres.
- Base probe `S0.1`, `S1.1` ([`tests/golden/base_probe.csv`](../../../tests/golden/base_probe.csv)).
