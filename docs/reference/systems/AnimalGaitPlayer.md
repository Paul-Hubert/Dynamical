# AnimalGaitPlayer

`AnimalGaitPlayerSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after `AnimalFactory`, before `PhysicsStep` (demonstration worlds `common/animal_demo_world.json`, `common/animal_demo_terrain_world.json`) · Parameters: no

Plays the procedural walk (`proceduralWalk`, through a `GaitWalk`) on the kinematic ragdoll, following its own
`WalkCommand`: the demonstrations GAIL learns from. It plays exactly the command process the physical animal's task
uses, so the recorded observations look like the policy's.

## Behaviour

- **init**: emplaces the [`AnimalWalkState`](../components/AnimalWalkState.md) context variable. Without a built
  `AnimalBot` its problem is "no animal"; with a dynamic body "the body is not kinematic" (logged). Else loads the
  species from the animal library (`AnimalLibrary::loadSpecies(animalsDirectory(), body.species)`), picks the clip of
  `AnimalSpawn::gait` (walk, trot or run), analyses it (`GaitWalk::setup` / `analyseGait`), binds the body to the
  skeleton (`animalBodyBind`) and is `ready`. With the walk gait, ranges that run (`ranges.run > 0`) and a body and
  species that run, sets up a second `GaitWalk` on the run clip (`has_run`). Any failure (no such clip, a clip the gait
  analysis cannot read, an exception) is kept in `problem` and logged; the training host reports it
  (`animalDemoProblem`) since such a recording never ends. A world without a ground field gets a `PlaneField` as the
  bot's ground. Sets `demo_restart`.
- **tick**: nothing until ready.
  - New walk (`demo_restart`, set by init and by every reset): a run from end to end with probability `ranges.run`
    (when it has one), else a walk; `walks++`; a style drawn from the ranges (`drawStyle`, soft ground per the spawn's
    `soft_depth` or the ranges), the episode's soft depth from it; heading the spawn's `heading` (degrees) or drawn,
    or on a terrain a start point and direction from the routes (`drawStart`); `GaitWalk::start` at a random phase;
    a first command (`beginCommand`, only walks / only runs when a run exists); warm-up of `warmup` seconds plus up to
    a cycle, rounded down to whole decision windows (at least one); `recorded = 0`; the bodies are teleported.
  - Counts decisions itself (no drive does): `decisions++`, `window_ticks = 0` on the first tick of every window.
  - `advanceCommand` at the clip's speed (the gait's walk / run clip speed, or the model's), then
    `GaitWalk::step(speed, turn, ground, h)`; the command's yaw follows the walker's.
  - With ranges that stand (`idle > 0`) and a walk: as the command's speed falls from 0.5 to 0.06 of the slowest walk,
    the pose is blended joint by joint (in each parent) into the body's standing pose (smoothstep).
  - `segmentPoses` gives each segment's world pose; the Jolt bodies are teleported (`SetPositionAndRotation`, new walk;
    `history_valid = false` so the next velocities are zero) or moved (`MoveKinematic` over `h`). `warm_left--`.

## Parameters

None (`AnimalSpawn`: `gait`, `speed`, `warmup`, `heading`, `soft_depth`, `recording_decisions`; the ranges file).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | init, tick | ground (plane when none), `demo_restart`, `walks`, `soft_depth`, `origin`, `route_lost`, `command`, `decisions`, `window_ticks`, `warm_left`, `recorded`, `history_valid` |
| [AnimalWalkState](../components/AnimalWalkState.md) | context | readwrite+create | init (create), tick | the walk and run, blended pose, bind, readiness |
| [WorldRng](../components/WorldRng.md) | context | readwrite | tick | run or walk, style, heading, phase, warm-up, command draws |
| [WorldClock](../components/WorldClock.md) | context | read | tick | `tick` and `decision_period`: windows and warm-up |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | tick | teleports or moves the kinematic bodies |

## Side effects

- Reads the species' files from the animal library (`resources/animals/`) in init.
- Moves the ragdoll's kinematic Jolt bodies every tick.
- Logs an error when the walk cannot be played.

## Order and dependencies

- After `AnimalFactory` (its init needs the built, kinematic bot), before `PhysicsStep`; `AnimalSync`, `AnimalChecks`
  and the observe phase read what it moved.
- `AnimalRewards` resets with `demo_restart`, which this system turns into the next walk. `AnimalDrive`,
  `AnimalTask`, `AnimalTermination` and `AnimalSoftGround` do nothing for a kinematic body.

## Used in scenes
<!-- generated -->

## Tests

- `AA.5` (the kinematic walk across its ranges never falls), `AA.6` (limits hold, the heading never flips, the speed is the commanded one), `AA.7` (a run), `AA.8` (terrain recording), `AA.11` (stands, walks and runs), in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp).
