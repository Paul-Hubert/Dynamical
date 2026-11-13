# AnimalAnimation

`AnimalAnimationSys` in [`src/logic/animals/animal.cpp`](../../../src/logic/animals/animal.cpp) (declared in [`animal.h`](../../../src/logic/animals/animal.h), registered from [`animal_lineup.cpp`](../../../src/logic/animals/animal_lineup.cpp)) · Scope: Host · Needs renderer: no
· Lanes: `host.pre` · Parameters: no

Plays the clips of every animal of the library: advances each `Animal`'s clip time (and the time of the clip it fades from), samples the keys of both, blends them, and writes the joints, their model space matrices and the skinning matrices into the entity's `AnimalPose`. `AnimalPass` draws from that pose, `AnimalGait` bends and plants it, riders and the debug menu read its joints.

## Behaviour

- **preinit**: makes the `Animal` and `AnimalPose` storages and calls `animalLibrary(reg)`, which emplaces the `AnimalLibrary` context variable and loads `resources/animals/library.json` (from `./resources/animals`, else the source tree's, `animalsDirectory()`) when the registry has none yet. A missing library is a warning, not an error: the library is empty and every animal is skipped.
- **tick(dt)**: for every entity with an `Animal`:
  1. `AnimalLibrary::species(animal.species)` (loads `species/<id>.json`, `.bin` skeleton and `.anim` clips on first use, on the main thread; a species that fails is logged once and stays null). No species: the entity is skipped (no pose).
  2. A `clip` past the species' clips becomes `-1` (the bind pose).
  3. `time += dt * rate`; while `fade < fade_duration`: `fade += dt`, `previous_time += dt * previous_rate`. `dt` is not clamped.
  4. An `AnimalPose` is emplaced when the entity has none.
  5. `evaluate()` per animal: `AnimalSpecies::sample(clip, time, in_place)` (loops wrap, others hold their ends; `in_place` takes the travel of the clip's motion joint out), and while fading the previous clip is sampled and blended joint by joint (smoothstep of `fade / fade_duration`, translation and scale lerped, rotations by the short way, normalized); then `worldMatrices` (parents first) and `skinMatrices` (world times inverse bind).
- Evaluation runs on the taskflow executor (`tf::Executor` context variable) in batches of 16 animals when there are more than 32, and the tick waits for it; otherwise on the calling thread. Each batch is a Tracy zone `Evaluate animal poses`.

After the tick every `Animal` whose species is loaded has an `AnimalPose` with `local`, `world` and `skin` of the joint count of its species, for this frame's clip time.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Animal](../components/Animal.md) | component | readwrite | tick | species and clip read; `time`, `fade`, `previous_time` advanced, an out of range `clip` reset to -1 |
| [AnimalPose](../components/AnimalPose.md) | component | write+create | tick | emplaced when missing; `local`, `world`, `skin` overwritten by `evaluate()` |
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read+create | preinit, tick (`animalLibrary`) | emplaced and loaded when missing; species loaded lazily into its cache |
| [Executor](../components/Executor.md) | context | read | tick | taskflow executor for the pose batches, optional |

## Side effects

- Files read: `resources/animals/library.json` (preinit, when the library is not there yet) and `species/<id>.json`, `.bin` (skeleton part), `.anim` of every species on first use.
- Log: the library's size or its absence (preinit); `Animals : cannot load <id>` once per species that fails.
- Taskflow tasks on the executor (one per batch of 16 animals), waited for within the tick.

## Order and dependencies

- After whatever chooses clips and moves animals this frame (`AnimalLineup`, `AnimalWalkers`, `Herds`, `SteeredWalk`), which call `Animal::play()`: the cross fade starts from this tick.
- Before `AnimalGait`, which reads the pose this system wrote and overwrites its `world` and `skin` matrices, and which sets `Animal::rate` used by the next tick.
- Before the renderer's `AnimalPass` (it reads `AnimalPose::skin`).

## Used in scenes

<!-- generated -->
- [`animals_gait.scene.json`](../../../resources/scenes/animals_gait.scene.json): `host.pre`
- [`animals_test.scene.json`](../../../resources/scenes/animals_test.scene.json): `host.pre`
- [`forest_herds.scene.json`](../../../resources/scenes/forest_herds.scene.json): `host.pre`
- [`forest_herds_watch.scene.json`](../../../resources/scenes/forest_herds_watch.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`
- [`tautavel_walk.scene.json`](../../../resources/scenes/tautavel_walk.scene.json): `host.pre`
- [`tautavel_walk_torch.scene.json`](../../../resources/scenes/tautavel_walk_torch.scene.json): `host.pre`

## Tests

- `ANIMAL.3` (`tests/unit/animals.cpp`): advances the clips and blends from the previous clip.
- Run inside `HERD.1`, `HERD.2` (`tests/unit/animals.cpp`) and `AGAIT.2` to `AGAIT.4` (`tests/unit/animal_gait.cpp`).
- `ANIMAL.1`, `ANIMAL.2`, `ANIMAL.library`: the library, sampling and skinning it relies on.
