# Herds

`HerdSys` in [`src/logic/animals/herd.cpp`](../../../src/logic/animals/herd.cpp) (declared in [`herd.h`](../../../src/logic/animals/herd.h), registered from [`animal_lineup.cpp`](../../../src/logic/animals/animal_lineup.cpp)) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, before `AnimalAnimation` · Parameters: yes

Herds of animals of the library that graze and move on over the terrain (docs/animals.md, "Herds"). The world is cut in square cells; each cell has a herd or none, drawn from a hash of the seed and the cell, so a cell has the same herd (kind, home, members, sizes) whenever the viewer comes back. Herds within `radius` of the viewer exist as entities (`Transform`, `Animal`, `HerdAnimal`, and `AnimalGait` for the procedural walk); a herd grazes around its centre, then travels to another place within `range` of its home on land that suits it. Its animals walk at the speed of their walk clip, turn at a limited rate, keep off each other and off the trunks of the trees, stand on the ground and lean with its slope. A member someone steers (`AnimalSteer`, a rider) is let go and taken back when it is no longer steered.

## Behaviour

- **constructor**: reads `centre`, `procedural`, `style` from the parameters; the rest is read in init.
- **preinit**: makes the `Animal`, `AnimalPose`, `HerdAnimal`, `AnimalSteer`, `SteeredWalk`, `AnimalGait`, `Transform`, `Plant` storages; emplaces the `HerdStats` context variable and puts `HerdSys*` (this system) in the context (`insert_or_assign`), for `HerdWatch`.
- **init**:
  1. The definition: the `definition` parameter as an object, or the JSON file `./resources/<definition>` (default `herds/tautavel.herds.json`); the parameters `seed`, `cell`, `density`, `radius`, `range`, `max_slope`, `graze` override its keys. `HerdDefinition::parse` keeps the members the library has and that have a walk clip (warnings for the others), drops kinds without members, a weight <= 0 or `count` min <= 0. A missing or malformed file, or cells under 10 m, is logged as an error and leaves the system without kinds (it then does nothing).
  2. The ground: context `std::shared_ptr<GroundField>`, else a `TerrainField` over the context `std::shared_ptr<TerrainGenerator>`, else a level `PlaneField`; the generator is also kept for the slope of the land. The context `std::shared_ptr<ForestField>` when there is one, for the `where` of the kinds.
  3. Logs the number of kinds, the cell size, the density, the radius.
- **tick(dt)** (`dt` clamped to 0.1 s; nothing without kinds):
  1. **Viewer**: the `centre` parameter, else the desktop `Camera` position, else (0, 0).
  2. **Despawn**: herds whose centre is further than `radius + 0.4 cell` from the viewer are despawned: members destroyed, except members with `AnimalSteer` or `SteeredWalk`, which only lose their `HerdAnimal` and stay in the world on their own.
  3. **Spawn** (every 0.5 s): every cell of the square `viewer ± radius` without a herd is described (`describe()`: density roll, kind by weight, a home in the middle 70 % of the cell that `suits` the kind in 10 tries: land slope from the generator's heightfield under `max_slope`, forest cover for `open` (< 0.25), `edge` (0.1..0.8), `forest` (> 0.6); members by weight with a scale in `scale` times the member's own, slots spread around the centre at least 0.7 `spacing` apart, extent `0.55 spacing sqrt(count)`, herd speed 0.85 of its slowest member's walk); a herd whose home is within `radius` is spawned: per member an entity with `Transform` (at y 0, rotation by its yaw, uniform scale), `Animal` (its species, the idle clip or the walk, time = a random offset), `HerdAnimal` (kind, cell) and, when `procedural`, an `AnimalGait` with `style`. Logged at debug level.
  4. **Trunks**: when the number of `Plant`s changed, at most twice a second, the trunks are gathered again: every `Plant` with `Transform` whose species (`VegetationLibrary`) is at least 2.5 m tall, radius `min(0.15 + 0.015 height, 0.6)`, in 8 m cells.
  5. **Herds** (`updateHerd`): a travelling herd moves its centre toward its destination at its speed (a quarter of it while its members are on average more than `1.5 extent + 3` m behind) and on arrival grazes for `graze` seconds; a grazing herd, when its timer runs out, looks for a destination within `range` of home, at least `3 extent + 10` m away, that suits it, with no slope over `max_slope + 6` on the way (12 tries; else again in 5 to 15 s).
  6. **Members** (`updateMember`): a steered member (`AnimalSteer` or `SteeredWalk`) is only followed: its position, height and heading copied from its `Transform`, `HerdAnimal` set standing. Others: when standing, on their timer, pick the next in-place clip (idle, or eat twice as often unless travelling; at least 8 frames, no motion joint) and sometimes a new spot near their slot; walk to `centre + spot` when further than their reach (1.5 times when travelling), stop at 0.3 reach; push away from herd mates closer than `0.45 (length + length)` and around trunks (never inside one); turn at `clamp(2.2 / length, 0.9, 4)` rad/s; speed `walk_speed` times alignment, arrival and catching up (1.3), changed by at most `2.5 walk_speed` per second; height from three ground samples, pitch eased to the slope (at most 0.6 rad). Above 0.1 of the walk speed they play the walk clip (rate `clamp(speed / clip speed, 0.5, 1.8)` unless they have an `AnimalGait`). `Transform` written (lifted by `-min.y * scale` for models standing on y = 0); `HerdAnimal::moving`, `speed` written.
  7. **Stats**: `HerdStats` rebuilt: herds, animals, moving, travelling, and a `HerdInfo` per herd (cell, kind, centre and extent of its members, count, travelling).
- **finish**: despawns every herd (as in tick: steered members survive without `HerdAnimal`).
- `describe()` and `nearest(kind, from, reach, home)` (the home of the nearest herd of a kind) are public: `HerdWatch` calls `nearest` through the `HerdSys*` context variable.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `definition` | string or object | `"herds/tautavel.herds.json"` | path under `./resources/`, or the definition itself |
| `seed` | uint | definition's (1) | seed of the herds of the cells |
| `cell` | float | definition's (180) | metres, size of a cell (at least 10) |
| `density` | float | definition's (0.7) | part of the cells that have a herd |
| `radius` | float | definition's (420) | herds whose home is this close to the viewer exist |
| `range` | float | definition's (140) | metres a herd goes from its home |
| `max_slope` | float | definition's (18) | degrees of land slope herds keep off |
| `graze` | [float, float] | definition's ([20, 60]) | seconds a herd stays before it moves on |
| `centre` | [x, z] | none | herds are kept around this point instead of the desktop camera |
| `procedural` | bool | true | members get an `AnimalGait` (procedural walk; `AnimalGait` after `AnimalAnimation` in the scene) |
| `style` | object | `GaitStyle` defaults | the `GaitStyle` of the members' procedural walk |

The definition file: `{ "cell", "density", "radius", "range", "max_slope", "graze", "seed", "herds": [ { "name", "weight" (1), "count" ([4, 10]), "spacing" (4), "scale" ([0.92, 1.08]), "where" ("any" / "open" / "edge" / "forest"), "members": { "<id>": weight } or { "<id>": { "weight", "scale" } } } ] }`.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Transform](../components/Transform.md) | component | readwrite+create+destroy | spawn, updateMember, despawn, rebuildTrunks | members' made, placed on the ground every tick (read when steered), destroyed with them; plants' read for trunks |
| [Animal](../components/Animal.md) | component | readwrite+create+destroy | spawn, updateMember, despawn | species, clip, time set at spawn; clips played (`play`), `rate`; destroyed with the member |
| [HerdAnimal](../components/HerdAnimal.md) | component | readwrite+create+destroy | spawn, updateMember, despawn | kind and cell at spawn; `moving`, `speed` every tick; removed from steered members when the herd goes |
| [AnimalGait](../components/AnimalGait.md) | component | write+create | spawn | the procedural walk's style, when `procedural` |
| [AnimalGait](../components/AnimalGait.md) | component | read+destroy | updateMember (`all_of`), despawn | with one, the rate is left to `AnimalGait`; destroyed with the member |
| [AnimalPose](../components/AnimalPose.md) | component | destroy | despawn | destroyed with the member (made by `AnimalAnimation`) |
| [AnimalSteer](../components/AnimalSteer.md) | component | read | updateHerd, updateMember, despawn | a steered member is let go, not waited for, not destroyed |
| [SteeredWalk](../components/SteeredWalk.md) | component | read | updateHerd, updateMember, despawn | the same while it coasts to a halt |
| [Plant](../components/Plant.md) | component | read | tick (storage size), rebuildTrunks | trunks to walk around |
| [HerdStats](../components/HerdStats.md) | context | write+create | preinit (emplace), tick | the herds of this frame, for `HerdWatch`, `CameraWalk`, the debug menu |
| [HerdSys](../components/HerdSys.md) | context | write+create | preinit | `HerdSys*` to this system, for `HerdWatch::nearest` |
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read+create | init, describe, spawn | members' species, sizes, walk clips |
| [Camera](../components/Camera.md) | context | read | tick | the viewer, without `centre` |
| [GroundField](../components/GroundField.md) | context | read | init | the ground (`std::shared_ptr`) |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | the ground without a field; slope of the land (`std::shared_ptr`) |
| [ForestField](../components/ForestField.md) | context | read | init | forest cover for the kinds' `where` (`std::shared_ptr`) |
| [VegetationLibrary](../components/VegetationLibrary.md) | context | read | rebuildTrunks | height of the plants' species |

## Side effects

- File read: the definition JSON under `./resources/` (init).
- Entities created (spawn) and destroyed (despawn, finish) as the viewer moves; destroying a member destroys every component on it (`Mountable` and the like added by other systems).
- Log: warnings for members missing from the library or without a walk, an error for a bad definition, info line at init, a debug line per herd spawned.
- Ground queries on the ground field per member per tick; heightfield samples of the generator for slopes.

## Order and dependencies

- Before `AnimalAnimation` (clip changes) and `AnimalGait` (reads the motion of the `Transform`).
- After the forest (`ForestSys` made the `ForestField` context and `Plant`s) to avoid trunks; works without.
- `HerdWatch` and `CameraWalk` read `HerdStats` after it.
- A member steered by `Mounting` / `SteeredWalk` is skipped while it has `AnimalSteer` or `SteeredWalk`.

## Used in scenes

<!-- generated -->
- [`forest_herds.scene.json`](../../../resources/scenes/forest_herds.scene.json): `host.pre`
- [`forest_herds_watch.scene.json`](../../../resources/scenes/forest_herds_watch.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`
- [`tautavel_walk.scene.json`](../../../resources/scenes/tautavel_walk.scene.json): `host.pre`
- [`tautavel_walk_torch.scene.json`](../../../resources/scenes/tautavel_walk_torch.scene.json): `host.pre`

## Tests

- `HERD.1` (`tests/unit/animals.cpp`): the same herds for a seed, on the ground, no faster than their walk, apart.
- `HERD.2`: lets go of a steered animal, takes it back, leaves it behind when the herd goes.
