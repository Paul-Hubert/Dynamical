# AnimalBotSteer

`AnimalBotSteerSys` in [`src/logic/animals/steer.cpp`](../../../src/logic/animals/steer.cpp) (declared in [`steer.h`](../../../src/logic/animals/steer.h)) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `WorldStepper` · Parameters: yes

The host's stand-in for the physical animal of each world (docs/players.md, "Riding"; docs/physical-agents/reports/animal-agents.md). The scene's worlds are other registries with their own physics; for every world whose context holds a built, non kinematic `AnimalBot`, this system keeps one host entity, an `AnimalBotProxy` with a `Transform` (the animal's model space where its trunk stands, upright, heading as the trunk) and an `AnimalPose` of the ragdoll's joints in it. A rider meets that proxy (`Mountables`, `Mounting`, `RiderSeat`); when the proxy has an `AnimalSteer`, the system turns it into the bot's `WalkCommand` (the command its policy observes), within the speeds and turns its ranges were trained with.

## Behaviour

- **preinit**: makes the `AnimalSteer`, `AnimalBotProxy` and `Transform` storages.
- **tick**: nothing without the `Worlds` context variable. For each world `w`:
  1. The world's `AnimalBot` context variable; skipped when there is none, it is not `built` or it is `kinematic` (demonstrations).
  2. Speeds from its body and ranges: `slowest = ranges.speed[0] * body.walk_speed`; `fastest = ranges.run_speed[1] * body.run_speed` when it runs (`ranges.run > 0` and a run speed), else `ranges.speed[1] * body.walk_speed`.
  3. **Proxy** (first time, or when the entity was destroyed): a new entity with `Transform` and `AnimalBotProxy` (`world`, `species` = index of `body.species` in the library or -1, `walk_speed`, `top_speed = fastest`); when the library has the species, the bind of the body to its skeleton (`animalBodyBind`) is kept and an `AnimalPose` emplaced.
  4. **Transform** of the proxy: rotation = heading of the trunk segment (`bot.rotation[root]` times the inverse of its spawn rotation) about +y; position = trunk position minus the rotated spawn position of the trunk.
  5. **Pose**: `skeletonFromSegments()` from the bot's segment positions and rotations, carried into the proxy's model space; written to `AnimalPose::world` only (`local` and `skin` are not written).
  6. **Command** (`bot.command`, a `WalkCommand`): with an `AnimalSteer` on the proxy, or without one when `free` is `stand`: `wanted_speed` = the steer's speed clamped to `[0, fastest]`, raised to `slowest` above 0.05 m/s, else 0; `wanted_turn` = `1.5 * (yaw asked - command.yaw) / speed`, clamped to the tightest trained radius (`ranges.radius[0]` or `run_radius[0]` when the speed is a run, in body lengths, at least 0.5); `until` = never (the task does not draw). Without a steer and `free` = `wander`: a command left at "never" gets `until = time`, so the world's `AnimalTask` draws again.
- **finish**: destroys the proxy entities.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `free` | string | `"stand"` | what the animal does without an `AnimalSteer`: `stand` (speed 0), `wander` (its task's own draws) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Worlds](../components/Worlds.md) | context | read | tick | the scene's worlds and their registries |
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | tick (in each world's registry) | body, ranges, segment states read; `command.wanted_speed`, `wanted_turn`, `until` written |
| [AnimalBotProxy](../components/AnimalBotProxy.md) | component | write+create | tick | one per world, filled at creation |
| [AnimalBotProxy](../components/AnimalBotProxy.md) | component | destroy | finish | proxies destroyed with their entity |
| [Transform](../components/Transform.md) | component | write+create | tick | created with the proxy, position and rotation overwritten every tick |
| [Transform](../components/Transform.md) | component | destroy | finish | destroyed with the proxy entity |
| [AnimalPose](../components/AnimalPose.md) | component | write+create | tick | emplaced when the species is in the library; `world` overwritten |
| [AnimalPose](../components/AnimalPose.md) | component | destroy | finish | destroyed with the proxy entity |
| [AnimalSteer](../components/AnimalSteer.md) | component | read | tick | heading and speed asked of the proxy (by `MountSteering`) |
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read+create | tick (`animalLibrary`) | species index and skeleton of the body |

## Side effects

- Creates one entity per world with an animal (on the first tick it is seen), destroys them in finish.
- Writes into other registries: the `AnimalBot::command` of each world's context; the world's `AnimalTask` / observation read it on the next world step.

## Order and dependencies

- After `WorldStepper` (the segments are this frame's) and before what rides the proxy (`Mountables`, `Mounting`, `MountSteering`, `RiderSeat`); the command written takes effect at the next world step, one frame later.
- Needs the worlds' `AnimalFactory` to have built the `AnimalBot`.

## Used in scenes
<!-- generated -->

## Tests

None directly (scene `player_deer`).
