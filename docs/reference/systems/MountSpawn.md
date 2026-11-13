# MountSpawn

`MountSpawnSys` in [`src/logic/player/mount.cpp`](../../../src/logic/player/mount.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, before `AnimalAnimation` (and `Mountables`) · Parameters: yes

A few animals of one library species standing in a row on the ground near the spawn, to get on. They are plain kinematic animals (`Animal` playing their idle clip, with `AnimalGait`): `SteeredWalk` walks them once they have a rider.

## Behaviour

- **preinit**: makes the pools of `Animal`, `AnimalPose`, `AnimalGait`, `Transform`.
- **init**: finds the species in the library (`animalLibrary`, loaded when missing); warns and stops when it is not
  there or fails to load. The ground is the `std::shared_ptr<GroundField>` context variable, else a `TerrainField` over
  the `TerrainGenerator` (with its own `MarchingCubes`), else none (y = 0). For `i` in 0..count: at `position.x +
  spacing x i`, `position.y` (z), on `GroundField::top`, lifted by `-min.y` of the species when its origin is near its
  feet; `Transform` turned `yaw + 25 x (i % 3 - 1)` degrees; `Animal` with the idle clip (else walk), time `0.7 x i`;
  `AnimalGait`.
- **tick**: nothing.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `species` | string | `"horse"` | library id of the animals |
| `count` | int | 3 | animals in the row |
| `position` | [x, z] | [4, -6] | the first animal (array of exactly two numbers, else ignored) |
| `spacing` | float | 3 | m along +X between animals |
| `yaw` | float | 90 | degrees about +Y of the middle animal of each three (the others +-25) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read+create | init (helper animalLibrary) | loaded when missing; the species (loaded on first use) and its idle / walk clips, bounds |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | `std::shared_ptr<TerrainGenerator>`: a TerrainField for the ground when there is no GroundField |
| [GroundField](../components/GroundField.md) | context | read | init | `std::shared_ptr<GroundField>`: the ground the animals stand on, when present |
| [Transform](../components/Transform.md) | component | write+create | preinit (storage), init | each animal at its place, turned by yaw +- 25 degrees |
| [Animal](../components/Animal.md) | component | write+create | preinit (storage), init | species, the idle clip (else walk), times staggered by 0.7 s |
| [AnimalGait](../components/AnimalGait.md) | component | write+create | preinit (storage), init | default: the procedural walk once steered |
| [AnimalPose](../components/AnimalPose.md) | component | read | preinit (storage only) | its pool is made so that later views over it exist; not otherwise touched |

## Side effects

- Creates `count` animal entities in init.
- Logs a warning `MountSpawn : no animal <species> in the library` and makes nothing when the species is missing.
- Without a GroundField but with a terrain, builds a TerrainField (with its own MarchingCubes) for the ground heights.

## Order and dependencies

After the terrain is in the context (`ChunkGenerator` or the scene's `GroundField`); before `AnimalAnimation` (poses them), `AnimalGait` and `Mountables` (makes them rideable).

## Used in scenes

<!-- generated -->
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`

## Tests

None in the unit tests; capture [`player_horse.capture.json`](../../../tests/capture/player_horse.capture.json).
