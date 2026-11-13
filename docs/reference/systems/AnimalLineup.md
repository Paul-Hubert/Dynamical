# AnimalLineup

`AnimalLineupSys` in [`src/logic/animals/animal_lineup.cpp`](../../../src/logic/animals/animal_lineup.cpp) (declared in [`animal_lineup.h`](../../../src/logic/animals/animal_lineup.h)) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, before `AnimalAnimation` · Parameters: yes

Puts the animals of the library in rows on the ground, the small ones in front, to look at them and at their clips (scene `animals_test`). Each animal plays the clips asked for one after the other. Positions are logged.

## Behaviour

- **preinit**: makes the `Animal`, `AnimalPose`, `AnimalShow` and `Transform` storages.
- **init**:
  1. Selects the species: every entry of the library, or the `species` ids (a missing one is a warning). Nothing when none is left.
  2. The ground: context `std::shared_ptr<GroundField>`, else a `TerrainField` over the context `std::shared_ptr<TerrainGenerator>`, else none (y from the generator's heightfield, or 0).
  3. Without `origin` and with a generator: `levelGround(generator, (12, 14)) + (0, 7)` (the middle of the most level 24 x 28 m within 400 m of the world's origin, by the heightfield; logged). Without either: (0, -12).
  4. With `camera`: the desktop `Camera` 8 m south (+z) of the origin, 3 m over the ground, yaw 0 (looking north, -z), pitch -0.25.
  5. Sorts the species by `max(length, height)` (then index), rows of `per_row` from the origin toward -z: each animal takes `max(width, 0.3 length) + gap` across, a row `max(length, width)` of its animals deep, `gap` between rows. Per animal whose species loads: an entity with `Transform` (on the ground, lifted by `-min.y` for a model standing on y = 0; rotated `yaw` degrees about +y), `Animal` (species) and `AnimalShow` (the indices of the clips that match `clips`: all, a kind, or a name; for a kind the species' plain clip of it first). Logged with its height and clip count.
- **tick(dt)**: for every `Animal` with an `AnimalShow` that has clips: counts the current clip down; when it is over (or the animal has no clip yet) moves to the next clip of its list (wrapping), `Animal::play(clip, 0.25)`, rate 1; a looping clip is held for `round(seconds / duration)` rounds (at least one), one that does not loop is played once and 0.5 s more.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `species` | [string] | all | ids of the library to show |
| `origin` | [x, z] | most level ground within 400 m (+7 m in z); (0, -12) without terrain | middle of the first row |
| `camera` | bool | true | the desktop camera is put in front of the first row |
| `per_row` | int | 10 | animals in a row (at least 1) |
| `gap` | float | 1 | metres between two animals and two rows |
| `clips` | string | `"all"` | `all`, a kind (`walk`, `idle`, `run`, `eat`, `rest`), a clip name, or `none` (the bind pose) |
| `seconds` | float | 4 | how long a looping clip is held |
| `yaw` | float | 180 | degrees about +y (180: facing +z, the camera) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Transform](../components/Transform.md) | component | write+create | init | one entity per animal, on the ground |
| [Animal](../components/Animal.md) | component | write+create | init | its species |
| [Animal](../components/Animal.md) | component | readwrite | tick | clip read; next clip played (`play`), `rate` = 1 |
| [AnimalPose](../components/AnimalPose.md) | component | read | preinit | storage only: the pool is made before the first view; AnimalAnimation writes the poses |
| [AnimalShow](../components/AnimalShow.md) | component | readwrite+create | init, tick | the clips to play; index and time left advanced |
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read+create | init, tick (`animalLibrary`) | entries, sizes, species and clips |
| [Camera](../components/Camera.md) | context | write | init | in front of the first row, with `camera` |
| [GroundField](../components/GroundField.md) | context | read | init | ground heights (`std::shared_ptr`) |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | level ground, heights without a field (`std::shared_ptr`) |

## Side effects

- Entities created in init (one per animal), never destroyed by it.
- Log: missing species (warning), the origin, each animal's position, height and clip count.
- Loads every species shown from the library files (first use).

## Order and dependencies

- Before `AnimalAnimation` in the scene (it makes the pose of the clip it starts).
- After the terrain generator is in the context (scene setup), for the ground.

## Used in scenes
<!-- generated -->

## Tests

None (scene `animals_test`).
