# SteeredWalk

`SteeredWalkSys` in [`src/logic/animals/steer.cpp`](../../../src/logic/animals/steer.cpp) (declared in [`steer.h`](../../../src/logic/animals/steer.h)) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `MountSteering`, before `AnimalAnimation` · Parameters: yes

Walks the kinematic animals someone steers (a rider, docs/players.md): an entity with an `AnimalSteer` (heading, speed, run) and a `Transform` walks over the ground toward the heading asked, its speed following the one asked like a damped spring, turning no faster than its size lets it; with an `Animal` it plays its idle, walk or run by its speed (`AnimalGait` after `AnimalAnimation` plants the feet). The walk state is a `SteeredWalk` component that stays until the animal has come to a halt after its `AnimalSteer` went, so that whoever moved it before (`Herds`) only takes it back standing.

## Behaviour

- **preinit**: makes the `AnimalSteer`, `SteeredWalk`, `HerdAnimal`, `AnimalBotProxy`, `Animal`, `AnimalGait` and `Transform` storages.
- **init**: the ground: context `std::shared_ptr<GroundField>`, else a `TerrainField` over the context `std::shared_ptr<TerrainGenerator>`, else a level `PlaneField`.
- **tick(dt)** (`dt` clamped to 0.1 s; nothing when it is 0):
  1. **Newly steered**: every entity with `AnimalSteer` and `Transform` but no `SteeredWalk` and no `AnimalBotProxy` (physical animals go through `AnimalBotSteer`) gets a `SteeredWalk`: path `Free`, speed = its `HerdAnimal::speed` (the speed its herd had it walking) or 0, yaw = heading of its `Transform`, height = its y, already placed.
  2. For every entity with `SteeredWalk` and `Transform` (steered or coasting to a halt):
     - Length and sole from the species (`AnimalLibrary` entry length times scale, `-min.y` when the model stands on y = 0), else the scale.
     - Speed: `wanted = max(AnimalSteer::speed, 0)` (0 without `AnimalSteer`); `acceleration += (response² (wanted - speed) - 2 response acceleration) dt`, `speed = max(speed + acceleration dt, 0)` (critically damped: no jump in the acceleration either).
     - Turn: while steered and moving, a yaw rate of `2.5 * (yaw asked - yaw)` clamped to `clamp(turn / max(length, 0.3), 0.6, 4)` rad/s, eased (6/s) into `yaw_rate`; the walker's curvature is `yaw_rate / max(speed, 0.05)` above 0.02 m/s, else 0 (no turning on the spot).
     - `stepWalker()` (gait_scene.cpp): moves the `Transform` along the heading, onto the ground (three height samples 0.3 length apart), pitched with the slope (eased 8/s, at most 0.6 rad).
     - Clips, with an `Animal`: `walk_speed` remembers the last walk speed asked; `running` turns on once a run is asked and the speed is over half the speed asked, off when no run is asked and the speed is under 1.25 times the last walk asked (or the clip's walk speed). Clip: idle; the walk above 0.1 of the walk clip's speed; the run (or the trot without one) while `running`. `Animal::play(clip, 0.35)`; rate 1 for the idle, else `clamp(speed / clip speed, 0.5, 1.8)` unless the animal has an `AnimalGait` (which sets the rate itself).
  3. Entities without `AnimalSteer` whose speed is under 0.02 m/s lose their `SteeredWalk`.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `response` | float | 2.5 | 1/s: how fast the speed follows the one asked (the spring's natural frequency) |
| `turn` | float | 2.2 | rad/s of turn for an animal a metre long (divided by its length, clamped to 0.6..4 rad/s) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalSteer](../components/AnimalSteer.md) | component | read | tick | heading, speed and run asked |
| [SteeredWalk](../components/SteeredWalk.md) | component | readwrite+create+destroy | tick | emplaced on newly steered animals, walker state advanced, removed once halted without steering |
| [Transform](../components/Transform.md) | component | readwrite | tick (`stepWalker`) | heading read at the start; position and rotation written over the ground |
| [HerdAnimal](../components/HerdAnimal.md) | component | read | tick | the speed the herd had the animal walking, to start from |
| [AnimalBotProxy](../components/AnimalBotProxy.md) | component | read | tick (exclude) | proxies of physical animals are left to `AnimalBotSteer` |
| [Animal](../components/Animal.md) | component | readwrite | tick | species read; clip played (`play`), `rate` written |
| [AnimalGait](../components/AnimalGait.md) | component | read | tick (`all_of`) | with one, the rate is left to `AnimalGait` |
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read | tick (`find`) | species, length and clips (species loaded lazily) |
| [GroundField](../components/GroundField.md) | context | read | init | the ground walked on (`std::shared_ptr`) |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | the terrain when there is no ground field (`std::shared_ptr`) |

## Side effects

- Ground queries on the ground field each tick per walking animal.
- Adds and removes the `SteeredWalk` component (which `Herds` watches to let go of and take back its members).

## Order and dependencies

- After `MountSteering` (writes `AnimalSteer`) and `Mounting` (adds / removes it).
- Before `AnimalAnimation` (clip changes, rate) and `AnimalGait` (reads the `Transform`'s motion).
- With `Herds`: either order works, `Herds` skips members that have an `AnimalSteer` or a `SteeredWalk`; the scene `player_horse` puts `Herds` first.

## Used in scenes
<!-- generated -->

## Tests

- `HERD.2` (`tests/unit/animals.cpp`): a herd lets go of a steered animal, takes it back once the `SteeredWalk` is gone, leaves it behind when the herd goes.
- `MOUNT.2` to `MOUNT.5` (`tests/unit/mount.cpp`): the riding fixture runs `SteeredWalkSys` (steering, run, getting off, the animal no longer steered).
