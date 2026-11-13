# BoxRing

`BoxRingSys` in [`src/view/systems/base_scene.cpp`](../../../src/view/systems/base_scene.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre (base) · Parameters: no

Part of the base scene (master's `SceneSys` split in three with ModelLineup and BoxThrower): at init, drops a ring of
twelve coloured boxes around the spawn point onto the marching cubes terrain.

## Behaviour

- **init**: for i in 0..11, angle = i x 30°, size = 1 + (i mod 4) m, at (25 cos, ground + size + 4, 25 sin) with the
  ground from `TerrainGenerator::surfaceHeight`; colour from the angle; `spawnBox` (dynamic box, no initial velocity),
  then the box is turned by the angle about normalize(1, 1, 0). Boxes are skipped when box.gltf does not load.
- preinit, tick, finish: nothing.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | surfaceHeight() under each box; required (ChunkGenerator makes it in preinit) |
| [ModelManager](../components/ModelManager.md) | context | readwrite | init (spawnBox) | load("./resources/models/box.gltf"): cached after the first call |
| [ModelC](../components/ModelC.md) | component | write+create | init (spawnBox, ModelManager::load) | the box model's entity, made the first time the file is loaded |
| [Transform](../components/Transform.md) | component | readwrite+create | init (spawnBox) | box entities; the rotation is set after spawnBox |
| [ModelRenderable](../components/ModelRenderable.md) | component | write+create | init (spawnBox) | draws box.gltf tinted with the colour |
| [RigidBody](../components/RigidBody.md) | component | write+create | init (spawnBox) | dynamic box, half_extent = size / 2, initial_velocity; RigidBodySys makes the Jolt body |

## Side effects

- 12 dynamic boxes with Jolt bodies (made by RigidBodySys) 4 m + size above the ground, rotated about (1, 1, 0).

## Order and dependencies

- Needs `std::shared_ptr<TerrainGenerator>` (ChunkGenerator's preinit; `get` fails without it) and `ModelManager`.
- Before RigidBody, TerrainColliders and Physics: the boxes get their bodies and terrain colliders before the first
  step; they fall onto the ground colliders.

## Used in scenes

<!-- generated -->
- [`base.scene.json`](../../../resources/scenes/base.scene.json): `host.pre`

## Tests

- `T2.4 base system list ...` (stub). GPU: base captures (`V0.1`, `V0.2`, `V1.1`, `V2.2`) and probes (`S0.1`, `S1.1`:
  the RigidBody positions over 900 ticks, `tests/golden/base_probe.csv`).
