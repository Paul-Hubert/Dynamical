# AnimalGait

`AnimalGaitSys` in [`src/logic/animals/gait.cpp`](../../../src/logic/animals/gait.cpp) (declared in [`gait.h`](../../../src/logic/animals/gait.h), registered from [`animal_lineup.cpp`](../../../src/logic/animals/animal_lineup.cpp)) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `AnimalAnimation` · Parameters: no

The procedural walk of the kinematic animals (docs/animals.md, "The procedural walk"). An animal with an `AnimalGait` that plays one of its gait clips (kind `walk`, `trot` or `run`) gets a pose made from how its `Transform` moves: the clip in place, the body bent along the curve of its path and banked into the turn, the feet planted on the ground where they land and carried over it to where they will land next, through inverse kinematics on the legs. It also sets the playback rate of the clip so that the steps match the speed over the ground.

## Behaviour

- **preinit**: makes the `AnimalGait`, `Animal`, `AnimalPose` and `Transform` storages; emplaces the `GaitModels` context variable when there is none.
- **init**: picks the ground the feet are put on: the context's `std::shared_ptr<GroundField>`, else a `TerrainField` over the context's `std::shared_ptr<TerrainGenerator>` (its own `MarchingCubes`), else a level `PlaneField`. Kept by the system.
- **tick(dt)** (`dt` clamped to 0.1 s), for every entity with `Transform`, `Animal`, `AnimalGait` and `AnimalPose`:
  1. Skips animals whose species is not loaded.
  2. When the `GaitStyleOverride` context variable exists and is `on`, its style replaces the animal's `AnimalGait::style` (the debug menu).
  3. The gait model of the clip: only for clips of kind `walk`, `trot`, `run`, from `GaitModels::get()` (analysed with `analyseGait()` on first use per species and clip, and logged). Not a gait clip or no valid model: `model = -1`, `weight = 0`, `started = false` (the feet are placed again next time) and the clip's pose is left as `AnimalAnimation` made it.
  4. A change of gait clip resets the walk (`started = false`, `weight = 0`). `weight` then rises to 1 over 0.3 s (the fade from the clip to the procedural walk).
  5. Pace: the stride is `max(model.speed, 0.05 height) * period * scale * style.stride`; `AnimalGait::period = clamp(stride / |speed|, 0.55, 1.8) * model.period`, and `Animal::rate = model.period / period`.
  6. `proceduralWalk()`: measures speed along the heading and turn rate from the change of the `Transform` (eased, 12/s), the phase from the clip time; carries every body joint to the arc of the path (curvature from turn / speed times `style.bend`, limited by `max_bend` between hind and fore hips, the neck and head turning `head_lead` more), banks (`bank`, at most 0.35 rad), lowers the body (`crouch` + `body_sink * soft_depth`); per leg, plants the foot at its landing target while the leg's stance phase lasts (sinking `soft_depth` over `sink_time`), else moves it along its swing curve (the clip's rise profile times `step_height`, at least `clearance`, out of soft ground first by `exit_vertical` / `exit_part`, into it by `entry_vertical`); lowers the body as much as the shortest leg needs (at most 0.12 of the height, down at once, back up at 6/s); solves each leg (`solveLeg`: CCD from the joint above the foot up to the hip, foot keeps its attitude), and rewrites `AnimalPose::skin`. `AnimalPose::local` keeps the clip's joints; `world` and `skin` are the walk's. Ground queries go to the system's ground field (`below`, then `top`).

## Parameters

None. The style of each animal is its `AnimalGait::style` (set by whoever made the animal: `AnimalWalkers`, `Herds`, tests), or the `GaitStyleOverride` when it is on.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Transform](../components/Transform.md) | component | read | tick | where the animal is: speed, turn, world frame of the feet |
| [Animal](../components/Animal.md) | component | readwrite | tick | species, clip and time read; `rate` written |
| [AnimalGait](../components/AnimalGait.md) | component | readwrite | tick (`proceduralWalk`) | style (overwritten by the override), feet, phase, period, speed, turn, bend, drop, model, weight |
| [AnimalPose](../components/AnimalPose.md) | component | readwrite | tick (`proceduralWalk`) | `world` read and bent, planted; `skin` rewritten |
| [GaitModels](../components/GaitModels.md) | context | readwrite+create | preinit (emplace), tick (`get`) | cache of the gait analysis per species and clip |
| [GaitStyleOverride](../components/GaitStyleOverride.md) | context | read | tick | style forced on every animal while `on` |
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read+create | tick (`animalLibrary`) | the species of each animal (loaded lazily) |
| [GroundField](../components/GroundField.md) | context | read | init | the ground the feet are planted on (`std::shared_ptr`) |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | the terrain, when there is no ground field (`std::shared_ptr`) |

## Side effects

- Log: one line per species and gait clip analysed (`Gait : <id> / <clip> : <n> legs, ...` or `no procedural walk (<why>)`).
- Ground queries on the `GroundField` (marching cubes of the terrain generator's densities when the field is a `TerrainField`), several per foot per frame.

## Order and dependencies

- After `AnimalAnimation`: needs the clip's `AnimalPose` of this frame and overwrites its `world` and `skin`.
- After whatever moves the animals' `Transform` this frame (`Herds`, `AnimalWalkers`, `SteeredWalk`): speed and turn are read from its change.
- The `rate` it writes is used by the next `AnimalAnimation` tick.
- Before `AnimalPass` (renderer) and anything reading the joints (`Mountables`' saddle).

## Used in scenes
<!-- generated -->

## Tests

- `AGAIT.1` (`tests/unit/animal_gait.cpp`): `analyseGait` reads the legs of quadrupeds, a bird and a man.
- `AGAIT.2` to `AGAIT.4`: `AnimalGaitSys` after `AnimalAnimationSys` on a plane: planted feet through turns, body bend, soft ground, steps following speed and style.
- `AGAIT.5`: the demonstrations (`writeGaitDemos`) built on `proceduralWalk`.
