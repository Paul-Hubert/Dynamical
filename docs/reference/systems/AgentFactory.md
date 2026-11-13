# AgentFactory

`AgentFactorySys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after `RigidBody` (and after `WorldTerrain`, `GroundTerrain` / `GroundPlane` where there are) · Parameters: no

Makes the HumanBot walker of a training world (docs/physical-agents/agent-runtime.md): reads the scene's
`HumanBotSpawn` entity, loads the body and params files, builds the Jolt ragdoll (one body per segment, one
swing-twist constraint with motors per joint) and the world's [`HumanBot`](../components/HumanBot.md) context
variable, configures the world's one [`AgentIO`](../components/AgentIO.md) (behaviour, 276 observations, 120 actions)
and starts the first episode. Every other system of the walker (`DriveTargets` ... `Episode`, `Energy`, the kinematic
walk of [GaitBase](GaitBase.md) and [RigPoseApply](RigPoseApply.md)) expects the `HumanBot` it makes.

## Behaviour

- **preinit**: `agentIO(reg)` creates the world's `AgentIO` context variable if it is missing (empty, not configured
  yet). This is what tells the training host the world has an agent before `init`.
- **init**:
  1. Takes the first entity with [`HumanBotSpawn`](../components/HumanBotSpawn.md). Without one: logs
     `AgentFactory: no entity with HumanBotSpawn` (error) and returns, leaving no `HumanBot` (the other systems then
     fail on `ctx().get<HumanBot>()`).
  2. Emplaces `HumanBot`; `loadBody(resourceFile(spawn.body))` and `loadParams(resourceFile(spawn.params))`
     (`resources/<path>` relative to the working directory, else next to the executable; both files hashed with
     SHA-256; `AssetError` on a malformed file). Copies `kinematic`, `recording`, `draw`, `view`, `view_speed`,
     `view_truncate`, `max_recording_ticks` from the spawn; `target_speed` from the params.
  3. Takes the world's ground field (`std::shared_ptr<GroundField>` context, from [GroundTerrain](GroundTerrain.md)
     or [GroundPlane](GroundPlane.md) preinit) into `HumanBot::ground`, and a copy of the spawn entity's
     `WalkRoutes` into `HumanBot::routes`. Routes without a ground field on a dynamic walker log an error
     (`WalkRoutes needs a ground field ...`) and are ignored by the episodes.
  4. `applyEnvParams` (the params file with the trainer's environment parameters on top, see below), the mass of the
     file, then `buildRagdoll` -> `buildRagdollParts` ([`ragdoll.cpp`](../../../src/logic/agents/ragdoll.cpp)):
     bodies at the rest pose on `PhysicsLayers::agent_layer`, dynamic or kinematic, masses = `totalMass x percentage`,
     inertia scaled from the file, friction / restitution / damping of the body file, no sleeping, `LinearCast` and
     enhanced internal edge removal when the world has `TerrainColliders` (marching cubes ground), a
     `GroupFilterTable` so no two segments collide, user data `(group << 32) | segment`. For a dynamic walker, one
     `SwingTwistConstraint` per joint (cone swing, twist limits, both motors in `PositionAndVelocity` with zero gains).
     Finds the hip / knee / ankle joints by name (`LeftUpLeg`, `LeftLeg`, `LeftFoot`, `Right...`) for DeVita's
     protocol, and `est0` (head over the lower foot at spawn).
  5. With `TerrainColliders` in the world: `insert_or_assign(TerrainUsers { bodies })` so that `WorldTerrain`
     builds colliders around the segments.
  6. `AgentIO::configure(params.behavior, 276, 120)`; sensor name `StackingSensor_size2_VectorSensor_size138`.
  7. With `draw`: one entity per shape of every segment with `Transform`, [`WorldShape`](../components/WorldShape.md)
     (box / sphere / capsule, half extents, segment, local pose) and `ModelRef` (no model, a colour by segment), for
     `WorldView`; [BodySync](BodySync.md) moves them.
  8. `resetHumanBot(reg)`: the first episode (see [Rewards](Rewards.md), which calls it at every later episode start).
- **tick**: nothing.
- **finish**: when the `HumanBot` was built, removes the constraints and removes / destroys the bodies; `built = false`.

### Episode reset (`resetHumanBot`, also called by Rewards)

`applyEnvParams`; in view mode no randomisation and `view_speed`; `drawSpeed` (continuous range or one of `speeds`
when randomising); `targetSpeed` from the env params overrides. Unless recording (the track keeps playing): new
mass properties when `totalMass` changed; on a terrain with routes, a spawn point and first direction from
`drawStart` (warning `no spawn point in the slope band after N draws`); segments at the spawn pose; `placeTarget`
(route direction x 1000 m, or a random target 20-30 m away when `randomTarget`, else 1000 m down -z);
[`applySlope`](#slope-applyslope) tilts the `Ground` boxes; random yaw / random initial tumble (2 m up, -4 m/s) when
randomising, facing the first direction on a terrain otherwise; the spawn pose lowered onto the ground under its
soles (+2 mm) on a terrain; bodies placed with `SetPositionRotationAndVelocity`, max angular velocity set, warm start
of the constraints reset, state read back, anchor history restarted. Then: the observation stack zeroed, the decision
snapshot, every counter and flag (`tick`, `decisions`, `dying`, `killed`, `truncated`, `pending_reset`), `fresh =
true`, the energy and formula books invalidated. `AgentIO`: action zeroed, reward 0, done / truncated cleared, stats
cleared then DeVita's episode stats published (`DeVita/NegPos ratio`, `.../Pos Work per Stance`, `.../Neg Work per
Stance`, `.../Accepted Stances`, when 4 stances or more) and added to the run totals. Eval legs restarted. `Parked`
set to false.

### Slope (`applySlope`)

Env params `slopeAngle` (default `HumanBot::slope_angle` = 0), `slopeRandom` (+angle / -angle, or with
`slopeIncludeLevel` also 0, drawn from `WorldRng`), `slopeSign` (fixed sign when not random; 0 = level),
`slopeCompensateSpeed` (default on: target speed x cos angle), `slopeFootReach` (0.25 m), `slopeLiftMargin` (0.01 m).
Every created `Ground` box is turned about the centre of its top face (rising toward -z) and dropped by
`reach x tan|angle| + margin` (`SetPositionAndRotation` on its static body). `HumanBot::slope_applied` keeps the angle.

## Parameters

No system parameters. The walker is configured by the `HumanBotSpawn` component and its files:

| Key (HumanBotSpawn) | Type | Default | Meaning |
|---|---|---|---|
| `body` | string | required | body file under `resources/` (`agents/bodies/*.body.json`) |
| `params` | string | required | params file under `resources/` (`agents/params/*.params.json`) |
| `motion` | `dynamic` / `kinematic` | `dynamic` | kinematic: no constraints, bodies moved by `TrackActions` or `RigPoseApply` |
| `recording` | bool | false | Unity's recording: header x, z zeroed, no randomisation, no death, episodes of `max_recording_ticks` |
| `draw` | bool | false | `WorldShape` entities for the viewer |
| `max_recording_ticks` | int | 1000 | episode length while recording |
| `view` | bool | false | no randomisation, fixed command speed `view_speed`, episodes never end by time |
| `view_speed` | float | 1.36 | m/s in view mode |
| `view_truncate` | bool | false | view with the training time limit (eval): eval legs and `EnergyFormulas` |

Environment parameters (`AgentEnvParams`, read at every reset, fall back to the params file): `rotationSpring`,
`rotationDamper`, `maxTorque`, `torqueFactor`, `maxAngularVelocity`, `maxTargetAngularVelocity`, `totalMass`,
`pushing`, `pushChance`, `pushForce`, `pulling`, `pullChance`, `pullForce`, `collisionPenalty`, `rewardFactor`,
`randomTarget`, `randomSpeed`, `randomInit`, `randomInitChance`, `randomize`, `rootReward`, `headSpringReward`,
`headDamperReward`, `winReward`, `deathPenalty`, `dyingHeightFactor`, `lifeReward`, `dyingReward`, `dieOnFall`,
`resetOnAlive`, `maxDyingFrames`, `arcFrames`, `continuousSpeed`, `targetSpeedMin`, `targetSpeedMax`,
`energyBonusWeight`, `energyScale`, `energySpeedEdge`, `energyMinSpeedFloor`, `energyVelocitySmoothingTau`,
`positiveEnergyMultiplier`, `negativeEnergyMultiplier`, `torqueRateWeight`, `statsSettleSeconds`, `targetSpeed`, and
the `slope*` keys above. Booleans are true above 0.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | context | readwrite+create | preinit (create), init, reset | the world's agent: configured, action / reward / done / stats cleared, DeVita stats published |
| [HumanBotSpawn](../components/HumanBotSpawn.md) | component | read | init | the files and modes of the walker (first entity only) |
| [HumanBot](../components/HumanBot.md) | context | write+create | init; finish (readwrite) | the walker's whole state; bodies removed at finish |
| [GroundField](../components/GroundField.md) | context | read | init | `std::shared_ptr<GroundField>` kept in `HumanBot::ground` |
| [WalkRoutes](../components/WalkRoutes.md) | component | read | init | copied into `HumanBot::routes` from the spawn entity |
| [TerrainColliders](../components/TerrainColliders.md) | context | read | init (`contains`) | the ground is a mesh: cast motion, `TerrainUsers` |
| [TerrainUsers](../components/TerrainUsers.md) | context | write | init | the segments' bodies, for `WorldTerrain` |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | init, reset, finish | bodies and constraints made, placed, removed |
| [AgentEnvParams](../components/AgentEnvParams.md) | context | read | init, reset (`applyEnvParams`, `applySlope`) | the trainer's environment parameters |
| [WorldRng](../components/WorldRng.md) | context | readwrite | reset | target speed, spawn point, target, yaw, initial tumble, slope sign |
| [Ground](../components/Ground.md) | component | read | reset (`applySlope`) | the flat ground boxes to tilt (their Jolt bodies are moved) |
| [Parked](../components/Parked.md) | context | write | reset | unparked at every episode start |
| [Transform](../components/Transform.md) | component | write+create | init (draw) | on the drawn shape entities |
| [WorldShape](../components/WorldShape.md) | component | write+create | init (draw) | one per segment shape |
| [ModelRef](../components/ModelRef.md) | component | write+create | init (draw) | colour of the drawn shape |

## Side effects

- Reads `resources/<body>` and `resources/<params>` (and hashes them); throws `AssetError` on a bad file.
- Creates 16 Jolt bodies and (dynamic) 15 swing-twist constraints in the world's `PhysicsSystem`; removes them at finish.
- Moves the static `Ground` bodies at every episode start (slope), sets body poses, velocities, mass properties.
- Creates the drawn shape entities (`draw`), never destroys them (they live as long as the world).
- Log: errors without a spawn entity or with routes but no ground field; warning when no spawn point is found in the
  slope band; error when `continuousSpeed` has an unusable range.

## Order and dependencies

- After `RigidBody` (its `init` creates the `Ground` bodies that the slope moves) and after the systems whose
  preinit makes the ground field (`GroundTerrain`, `GroundPlane`) or the `TerrainColliders` (`WorldTerrain`).
- Before every walker system and before `GaitBase` / `Fabrik` / `RigPoseApply` / `GaitRoutes` (they need the
  `HumanBot`), and before `TrackActions` (its `init` checks the track's body against the bot).

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/humanbot.cpp`: T4.4, T4.5 (Jolt mass properties equal the file), T4.6 (no pair of segments collides),
  T4.7 (malformed body refused), T4.8 (spawn clearance), S4.1, S4.3 (episodes run, kills, resets), DeVita's episode
  stats leave with the reset.
- `tests/unit/terrain_walker.cpp`: TW.4, TW.5 (spawn on the terrain), TW.6 (next episode elsewhere), TW.8, TW.11.
- `tests/unit/humanbot_rig.cpp`: S5.1 to S5.5 (kinematic rig).
