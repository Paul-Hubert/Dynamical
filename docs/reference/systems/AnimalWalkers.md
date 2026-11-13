# AnimalWalkers

`AnimalWalkersSys` in [`src/logic/animals/gait_scene.cpp`](../../../src/logic/animals/gait_scene.cpp) (declared in [`gait_scene.h`](../../../src/logic/animals/gait_scene.h), registered from [`animal_lineup.cpp`](../../../src/logic/animals/animal_lineup.cpp)) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, before `AnimalAnimation` and `AnimalGait` · Parameters: yes

Animals that walk side by side, each along a path of its own (a circle, a figure of eight, a weave, a straight line), to look at the procedural walk (scene `animals_gait`). Each animal is an `Animal` playing its walk, trot or run clip, an `AnimalWalker` moved over the ground by `stepWalker()`, and, with `procedural`, an `AnimalGait` with the style given. The camera can go round one of them.

## Behaviour

- **constructor**: `species` as an array of ids or as a string `"a,b,c"` (a scene variable; empty: the defaults); the other parameters below; `style` parsed with `GaitStyle::parse`.
- **preinit**: makes the `Animal`, `AnimalPose`, `AnimalGait`, `AnimalWalker`, `Transform` storages.
- **init**:
  1. Default species: `horse_farm`, `deer_red`, `wolf_2`, `rhinoceros`, `bearbrown`, `lioness`, `cat`, `crow`, `homo_heidelbergensis_walking`.
  2. The ground: context `std::shared_ptr<GroundField>`, else a `TerrainField` over the context `std::shared_ptr<TerrainGenerator>`, else a `PlaneField`. Kept by the system.
  3. Per id: the species (missing: warning); the clip of `gait` (`run`, `trot`, else the walk; the walk when the species has not the one asked; none: warning, skipped); the path radius `radius * max(length, 0.3)`.
  4. Without `origin` and with a generator: `levelGround(generator, (half the total width, 2 x the widest radius))`. With `camera`: the desktop `Camera` `widest + 9` m south (+z) of the origin, 3.5 m over the ground, yaw 0, pitch -0.3.
  5. Side by side along x, `gap` apart: per animal an entity with `Transform` (on the right of its circle, x + radius, unless the path is straight; heading -z), `Animal` (species, clip), `AnimalWalker` (path, radius, speed = `speed` times the clip's speed, or 0.6 length a second when the clip says none) and, with `procedural`, `AnimalGait` (`style`); without it `Animal::rate = speed`. Logged.
- **tick(dt)** (`dt` clamped to 0.1 s):
  1. Every entity with `Transform`, `Animal`, `AnimalWalker`: `stepWalker(walker, transform, length * scale, sole, ground, dt)`: the heading turned by `walkerCurvature()` (circle `1 / r`; eight: a loop to the left then one to the right; weave `sin(travelled / 1.5 r) / r`; straight 0; free: `turn`) times the distance covered, the position moved along it, put on the ground (three samples 0.3 length apart), pitched with the slope (eased 8/s, at most 0.6 rad); sole `min.y * scale` for a model standing on y = 0.
  2. With `follow` = n (a valid walker): the camera turns round the n-th animal at `orbit` degrees a second, `1.9 size + 0.8` m from it and `0.35 size + 0.2` m above its back's height (`0.45 height`), looking at it.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `species` | [string] or string | the nine above | ids, or `"a,b,c"` |
| `path` | string | `"circle"` | `circle`, `eight`, `weave`, `straight` (anything else: circle) |
| `radius` | float | 2.5 | of the turns, in lengths of the animal (at least 0.3 m length) |
| `speed` | float | 1 | times the speed of the clip |
| `gait` | string | `"walk"` | `walk`, `trot`, `run`: the clip played |
| `style` | object | `GaitStyle` defaults | the procedural walk's style for every animal |
| `procedural` | bool | true | false: no `AnimalGait`, the clip as it is (feet sliding), rate = `speed` |
| `origin` | [x, z] | most level ground within 400 m; (0, 0) without terrain | middle of the line of animals |
| `camera` | bool | true | the desktop camera in front of them at init |
| `gap` | float | 3 | metres between the paths of two animals |
| `follow` | int | -1 | n >= 0: the camera goes round the n-th animal |
| `orbit` | float | 6 | degrees a second of that turn |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Transform](../components/Transform.md) | component | readwrite+create | init, tick (`stepWalker`) | made at init; heading read on the first step, position and rotation written; the followed one read |
| [Animal](../components/Animal.md) | component | write+create | init | species, clip, rate without procedural |
| [Animal](../components/Animal.md) | component | read | tick | species: size and sole |
| [AnimalWalker](../components/AnimalWalker.md) | component | readwrite+create | init, tick | path, radius, speed; yaw, travelled, height, pitch advanced |
| [AnimalGait](../components/AnimalGait.md) | component | write+create | init | the style, with `procedural` |
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read+create | init, tick (`animalLibrary`) | species, sizes, clips |
| [Camera](../components/Camera.md) | context | write | init, tick | placed at init; round the followed animal |
| [GroundField](../components/GroundField.md) | context | read | init | the ground walked on (`std::shared_ptr`) |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | level ground, the ground without a field (`std::shared_ptr`) |

## Side effects

- Entities created in init (one per animal), never destroyed by it.
- Log: missing species or walk (warnings), each animal's clip, speed, radius and place.
- Ground queries per animal per tick.

## Order and dependencies

- Before `AnimalAnimation` and `AnimalGait` in the scene (the procedural walk reads the motion of the `Transform` this system wrote).

## Used in scenes
<!-- generated -->

## Tests

None for the system. `AGAIT.2` to `AGAIT.4` (`tests/unit/animal_gait.cpp`) use `AnimalWalker` paths and `walkerCurvature()`.
