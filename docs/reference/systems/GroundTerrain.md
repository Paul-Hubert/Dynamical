# GroundTerrain

`GroundTerrainSys` in [`src/logic/agents/ground_field.cpp`](../../../src/logic/agents/ground_field.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list (only its preinit does anything), usually as the replacement of `GroundPlane` · Parameters: yes

Makes the marching cubes terrain the world's [`GroundField`](../components/GroundField.md): a `TerrainField` that
samples the [`TerrainGenerator`](../components/TerrainGenerator.md)'s densities on the cube grid and meshes them with
the same marching cubes as the renderer and the colliders, so a foot planted on the field sits on the drawn and
collided ground.

## Behaviour

- **constructor**: reads `seed`.
- **preinit**: takes the world's `std::shared_ptr<TerrainGenerator>` when there is one (made by `WorldTerrain` before
  it, which then decides the seed: `seed` is ignored), otherwise emplaces a new `TerrainGenerator(seed)`. Then
  `insert_or_assign` the context variable `std::shared_ptr<GroundField>` with a `TerrainField(generator, new
  MarchingCubes)`.
- **tick**: nothing.
- The field is lazy: blocks of 4³ cubes (16 m) are sampled and meshed on the first query that touches them and kept;
  the cache is cleared when it reaches 4096 blocks. `density` is trilinear between the grid samples; `below` finds the
  highest triangle under a point within a distance, block by block downwards; `top` starts above the generator's
  surface height plus overhang amplitude + 8 m.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `seed` | int | 1337 | seed of the generator when the world has none yet (base's terrain is 1337) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read+create | preinit | reused, or created with `seed` (`std::shared_ptr`) |
| [GroundField](../components/GroundField.md) | context | write+create | preinit | the terrain field, as `std::shared_ptr<GroundField>` |

## Side effects

- CPU work on later queries (by other systems): FastNoise2 sampling and marching cubes of the blocks queried, memory
  for up to 4096 cached blocks. The cache is `mutable` and not locked: one world's field is used from one thread.

## Order and dependencies

- After `WorldTerrain` when both are in the list, so that the field and the colliders share one generator (and seed).
- Its preinit comes before every init, so `AgentFactory`, `AnimalFactory` and the others find the field there.

## Used in scenes

<!-- generated -->
- [`common/animal_demo_terrain_world.json`](../../../resources/scenes/common/animal_demo_terrain_world.json): `fixed`
- [`common/animal_terrain_world.json`](../../../resources/scenes/common/animal_terrain_world.json): `fixed`
- [`common/humanbot_terrain_world.json`](../../../resources/scenes/common/humanbot_terrain_world.json): `fixed`
- [`ik_check_terrain.scene.json`](../../../resources/scenes/ik_check_terrain.scene.json): `fixed`
- [`ik_walk_terrain.scene.json`](../../../resources/scenes/ik_walk_terrain.scene.json): `fixed`
- [`walker_demo_terrain.scene.json`](../../../resources/scenes/walker_demo_terrain.scene.json): `fixed`
- [`walker_demo_terrain_view.scene.json`](../../../resources/scenes/walker_demo_terrain_view.scene.json): `fixed`

## Tests

- `IK.8`, `GAIT.6`, `GAIT.14` in [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp)
- `TW.3`, `TW.5` to `TW.8`, `TW.10` in [`tests/unit/terrain_walker.cpp`](../../../tests/unit/terrain_walker.cpp)
