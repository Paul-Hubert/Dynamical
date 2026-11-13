# AnimalFactory

`AnimalFactorySys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after `RigidBody` (and after `GroundPlane` / `GroundTerrain` / `WorldTerrain` where the world has them), before every other `Animal*` system · Parameters: no

Makes the one physical animal of a training world (docs/physical-agents/reports/animal-agents.md): reads the scene's
`AnimalSpawn` entity, loads the body file, the shared params and the gait ranges it names, builds the ragdoll on Jolt,
configures the world's agent (`AgentIO`: behaviour `Animal_<id>`, observation and action sizes) and starts the first
episode. Every other `Animal*` system of the world works on the `AnimalBot` context variable it creates.

## Behaviour

- **preinit**: makes sure the world has an `AgentIO` context variable (`agentIO(reg)` finds or emplaces it), so that
  the training host sees an agent before `init`.
- **init**:
  1. Takes the **first** entity with an [`AnimalSpawn`](../components/AnimalSpawn.md) (others are ignored). With none
     it logs `AnimalFactory: no entity with AnimalSpawn` (error) and returns: there is then no `AnimalBot`, and the
     other `Animal*` systems, which `ctx().get<AnimalBot>()`, fail.
  2. Emplaces the [`AnimalBot`](../components/AnimalBot.md) context variable and fills it with `loadAnimalAssets`:
     the body (`loadAnimalBody(animalBodyPath(spawn.body))`: the path as given when it exists, else
     `resources/animals/bodies/<id>.body.json` of the library), the params file (`loadAnimalParams`, format
     `animal_params` version 1, SHA-256 kept in `file_hash`) and the ranges (`GaitRanges::parse`, SHA-256 in
     `ranges_hash`), both resolved with `libraryResourceFile`. A missing or bad file throws (`AssetError`). Copies the
     spawn's flags (`kinematic`, `recording`, `draw`, `view`, `view_truncate`).
  3. Takes the world's ground field (`std::shared_ptr<GroundField>` context variable, from `GroundPlane` or
     `GroundTerrain`) when there is one (null on the flat ground prefab: heights are then measured from y = 0), and a
     copy of the spawn entity's `WalkRoutes` when it has one (terrain worlds: spawn points and headings drawn in the
     slope band).
  4. `applyEnvParams`: `bot.params` = the params file with the trainer's environment parameters over them
     (`rootReward`, `deathPenalty`, `lifeReward`, `rewardFactor`, `fallHeight`, `softResistance`, `softLift`,
     `headReward`, `modeReward`, and the booleans `dieOnFall`, `randomYaw`: > 0 is true).
  5. `buildAnimalRagdoll`: `buildRagdollParts` on the world's `PhysicsWorld` with the body's mass and maximum angular
     velocity, kinematic bodies for a kinematic spawn, `LinearCast` when the world has `TerrainColliders`, group 0, no
     animal layer. Sizes the per segment arrays, `frame_size = 7 + 9 x segments`, reads the state of the bodies and
     finds the head (`findHead`: the segment whose name contains "head", else the most forward one that is neither a
     leg nor allowed to touch; the neck chain up to the shoulders; the muzzle point on the head's first shape; the
     muzzle height and head direction standing).
  6. With terrain colliders in the world, sets `TerrainUsers` to the ragdoll's bodies (`insert_or_assign`), so that
     `WorldTerrain` builds the colliders under them.
  7. Configures `AgentIO`: `configure(body.behavior, 2 x frame_size, 8 x joints)`, sensor name
     `StackingSensor_size2_VectorSensor_size<frame_size>`, clears the extra sensors and, when the spawn has `orders`,
     adds the vector sensor `nogail_order` (7 values: head yaw and pitch over their limits, the mode one-hot). With
     orders the neck may also touch the ground (`neck_may_touch`).
  8. With `draw`: one entity per shape of every segment, with `Transform`, `WorldShape` (segment, local pose, box /
     sphere / capsule and its half extents) and `ModelRef` (no path, a colour by segment), for `WorldView`.
     `AnimalSync` moves them.
  9. `resetAnimalBot`: the first episode (see [AnimalRewards](AnimalRewards.md), which calls it for every later one).
     For a dynamic body: soft depth of the episode (the spawn's `soft_depth` x height when >= 0, else drawn from the
     ranges' `soft` probability and `soft_depth` band), spawn point and yaw (`drawStart` on the routes, else the origin
     and a random yaw when `random_yaw` and not a view without truncation), `standAnimalBot` (the standing pose, the
     trunk over the origin, raised onto the ground field when routed, zero velocities, warm starts reset), the first
     command (`startCommand` from the ranges, or the spawn's fixed `speed` straight ahead), with orders a first order
     (`drawOrder`) and its speed factor. For a kinematic body only `demo_restart = true` (`AnimalGaitPlayer` starts the
     walk). Then the snapshots, counters and episode record are cleared, `AgentIO`'s action, reward, done, truncated
     and stats are zeroed and the world is unparked.
- **tick**: nothing.
- **finish**: destroys the ragdoll's bodies and constraints (`destroyAnimalRagdoll`) when the `AnimalBot` exists and
  was built. The shape entities are left to the registry.

## Parameters

None. What the animal is comes from the [`AnimalSpawn`](../components/AnimalSpawn.md) component (body, params file,
ranges, gait, motion, recording, draw, view, view_truncate, speed, warmup, recording_decisions, heading, soft_depth,
orders) and from `resources/agents/params/animal.params.json` (`AnimalParams`: `max_frames_60hz` 2000, `root_reward`
0.34, `death_penalty` 0, `life_reward` 0, `reward_factor` 1, `die_on_fall` true, `fall_height` 0.55, `touch_margin`
0.004, `random_yaw` true, `soft_resistance` 3, `soft_lift` 0.25, `over_speed` 0, `head_reward` 0.3, `mode_reward` 0.4).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | context | readwrite+create | preinit, init, resetAnimalBot | created in preinit; configured (behaviour, sizes, sensor name, `nogail_order` sensor); action, reward, done, truncated, stats cleared by the reset |
| [AnimalSpawn](../components/AnimalSpawn.md) | component | read | init | what animal to make (first entity only) |
| [WalkRoutes](../components/WalkRoutes.md) | component | read | init | copied into the bot: spawn points and headings on a terrain |
| [AnimalBot](../components/AnimalBot.md) | context | write+create | init, resetAnimalBot; finish (read) | created and filled; finish destroys its ragdoll |
| [GroundField](../components/GroundField.md) | context | read | init | `std::shared_ptr<GroundField>` kept in the bot as its ground |
| [AgentEnvParams](../components/AgentEnvParams.md) | context | read | init, resetAnimalBot (applyEnvParams) | trainer overrides of the params |
| [TerrainColliders](../components/TerrainColliders.md) | context | read | init, buildAnimalRagdoll | presence only: bodies get `LinearCast`, `TerrainUsers` is set |
| [TerrainUsers](../components/TerrainUsers.md) | context | write+create | init | the ragdoll's bodies, for `WorldTerrain` |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | init (buildAnimalRagdoll, standAnimalBot), finish | bodies and constraints created, placed, destroyed |
| [WorldRng](../components/WorldRng.md) | context | readwrite | resetAnimalBot | soft depth, yaw, spawn point, command and order draws |
| [Parked](../components/Parked.md) | context | write | resetAnimalBot | unparks the world |
| [Transform](../components/Transform.md) | component | write+create | init (draw) | on the shape entities |
| [WorldShape](../components/WorldShape.md) | component | write+create | init (draw) | one per shape of a segment |
| [ModelRef](../components/ModelRef.md) | component | write+create | init (draw) | colour of the drawn shape |

## Side effects

- Reads the body file, the params file and the ranges file (the last two hashed with SHA-256); throws `AssetError` when one is missing.
- Creates the ragdoll's Jolt bodies and swing-twist constraints in the world's physics system; destroys them in finish.
- Creates one entity per shape of every segment when `draw` is set (never destroyed by the system).
- Logs an error without an `AnimalSpawn`; the reset logs a warning when no spawn point is found in the slope band (`AnimalBot: no spawn point in the slope band after <n> draws`).

## Order and dependencies

- After `GroundPlane` / `GroundTerrain` (their preinit sets the ground field it reads) and `WorldTerrain` (its preinit
  creates `TerrainColliders`); `RigidBody` before it in the templates.
- Before every other `Animal*` system: they all need the `AnimalBot` (their own preinit runs before this init, but
  their ticks come after). `AnimalGaitPlayer`'s init, after this one, needs the built bot.
- The training host reads `AgentIO` after init (behaviour, sizes, sensors).

## Used in scenes
<!-- generated -->

## Tests

- `AA.1`, `AA.2` (`animal_stand`, `animalStandTest`), `AA.3` (layout of observation and actions, behaviour name), `AA.4`, `AA.5`, `AA.8` (terrain worlds spawn on routes), in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp); skipped without the generated bodies.
