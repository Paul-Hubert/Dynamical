# Sandbox

`SandboxSys` in [`src/logic/sandbox/sandbox.cpp`](../../../src/logic/sandbox/sandbox.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, between `TrainerActions@act` (or `Policies@act`) and `PhysicsStep` · Parameters: yes

The sandbox of [action agents](../../action-agents.md): animals of several species (deer and wolves by default)
living in one arena, each a ragdoll ([AnimalBot](../components/AnimalBot.md)) with two agents, its walker
(`Animal_<id>`, a decision every window) and its mind (`Action_<id>`, a decision every `mind_every` windows).
Sandbox owns the world's [Sandbox](../components/Sandbox.md) context and, on the first tick of every decision
window, applies the rules that change who is in the world and turns the agents' last decisions into what the
bodies do. [SandboxLife](SandboxLife.md) (after the physics step) and [SandboxObserve](SandboxObserve.md) (Observe
phase) do the rest.

## Behaviour

**preinit**
- Emplaces the [Sandbox](../components/Sandbox.md) context and, when the world has none, an
  [AgentsReset](../components/AgentsReset.md) (the host's RESET is handled by SandboxObserve).
- Reads the life params file (`life`, resolved by `libraryResourceFile`: build folder, else the source tree;
  JSON with comments). Throws `std::runtime_error` when it cannot be opened. The scene's `arena` replaces the
  file's; each block of `species` is merged (`json::update`) into the file's species of that id (an unknown id
  throws); a non-empty `only` keeps those species alone (an unknown id throws). `LifeParams::parse` gives
  `Sandbox::base` (with the file's SHA-256), copied to `params`, then the environment parameters `lifeScale`,
  `walkerShare`, `rewardScale` of [AgentEnvParams](../components/AgentEnvParams.md) are applied (`lifeScale` at
  least 0.01).
- Per species: loads its generated body `animals/bodies/<id>.body.json`, arms it with its weapon
  (`armAnimalBody`: a capsule on the body named `weapon_segment`; throws when the body has no such segment), and
  makes its growth stages: with `young_scale < 1`, two smaller bodies (`scaleAnimalBody` at `young_scale` and
  half way to 1, from ages 0 and `adult / 2`) before the adult (from `adult`); otherwise the adult alone. The
  walker params are `agents/params/animal.params.json`, ranges `GaitRanges::defaults()`, behaviours the body's
  (`Animal_<id>`) and `Action_<id>`, the reward program `<rewards>/<id>.rewards.json` (no file: no terms; a term
  that does not parse throws).

**init**
- Makes the grass field (`GrassField(params.grass, arena, WorldInfo::seed)`), installs the contact listener
  (`SandboxSys::Contacts`) on the world's Jolt `PhysicsSystem`, and adds four static wall boxes (2 m thick, 6 m
  high, friction 0.3) round the square of half side `arena`.
- `draw`: one flat box entity per 5 m square that is mostly grass (13 of its 25 metre samples), coloured darker
  where the grass is taller.
- `populate`: removes any animal, zeroes the species counters, and spawns `start` animals of each species at free
  places (85 % of the arena, at least 1.5 body lengths + 2 m from the others, 40 attempts), random yaw, adults of
  a random age between `adult` and half of `old` with 0.6 to 1 of a store, alternating sexes.
  `spawnSandboxAnimal` builds the ragdoll in group `next_group++` (colliding with other animals), stands it,
  gives it a fresh `WalkCommand` along its yaw, and creates its agents: the walker's
  [AgentIO](../components/AgentIO.md) on the animal (not in puppet worlds; stacked observation of 2 frames,
  sensor `nogail_order`) and a second entity with [SandboxMind](../components/SandboxMind.md) and the mind's
  AgentIO (39 floats, 4 continuous actions, one branch of 5 modes, a buffer `Interests` of `interests` rows of
  12). `draw` worlds also get the bodies as shapes.
- Logs a debug line per species.

**tick** (every tick; the window work only when `WorldClock::tick % decision_period == 0`)
1. Environment parameters applied again (`applyEnv`).
2. Dead animals whose terminal steps went out (`reported`): the mind's entity is destroyed, the walker's
   AgentIO removed, the ragdoll goes limp (zero drives, motors off). A carcass with no meat left, or older than
   `rot` (x life scale), is removed (`removeAnimal`: shapes, mind, ragdoll, entity).
3. Animals to stand again (`restand`): stood where they are, facing where they faced; a growth (`grow`) first
   rebuilds the ragdoll at the next stage's body (and its shapes); a fall instead stuns it for `stun` seconds.
   Command speed and turn zeroed, previous frame cleared, the walker's episode fresh, its AgentIO's
   `done`/`truncated` cleared and actions zeroed.
4. Births: a pregnant female whose gestation reached `gestation` (x life scale), while the species is under `max`,
   gives birth to a newborn (age 0, `newborn_energy`) beside her (1.2 body lengths to a random side, clamped to 95 %
   of the arena); she pays `birth_cost` of her store (keeps at least 0.02); both parents get +1 objective, a birth
   in their window and an offspring.
5. Newcomers: while a species has fewer than `min` alive, an adult spawns on the edge of the arena (92 %),
   facing the middle (`spawned` counted).
6. `heuristic`: every mind that asked (`requested`, not done) and nothing answered gets `sandboxHeuristic`'s
   action; `requested` and its stats are cleared.
7. Every living animal: `mindOrders` reads its mind's action (clamped to -1..1, non finite = 0) and discrete mode:
   speed = max(a0, 0) x `top_speed` x walk speed, turn = a1 / (`turn_radius` x length), head yaw / pitch from a2 /
   a3 over their limits; stunned animals rest with the head straight; Mate and Rest stand still, Feed is capped at
   half `feed_speed`. The command's yaw is reset to the trunk's when they are more than 0.5 rad apart (not in
   puppet worlds). Then the walker's action becomes the joint drives (`driveAnimalBot`, strength `weak + (1 -
   weak) x health`); a puppet only counts a decision.

Every tick, for each living animal: the command eases toward the wanted speed and turn (rate 2 / s), its yaw turns
by `turn x speed x dt`; a puppet's origin moves along it (clamped to 97 % of the arena) and its bodies are put in
the standing pose there (`placePuppet`, kinematic, without activating).

**finish**: removes every animal, clears the contact listener if it is still its own, removes and destroys the
walls.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `life` | string | `agents/params/life.params.json` | resources path of the life params (rules, grass, species) |
| `rewards` | string | `agents/rewards` | resources folder of `<species>.rewards.json` |
| `puppet` | bool | false | a check of the rules and minds: kinematic bodies in their standing pose slid along their command, no walker agents |
| `heuristic` | bool | false | the scripted minds answer every mind no policy answered |
| `draw` | bool | false | shapes per body and grass tiles for WorldView |
| `arena` | float | the file's (100) | m, half side of the square |
| `species` | object | none | `{ "<id>": { "start": n, ... } }` merged into the file's species |
| `only` | array | none | ids of the species kept |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Sandbox](../components/Sandbox.md) | context | write+create | preinit | made from the life params file with the scene's overrides, the species' bodies by growth stage, walker params and reward programs |
| [Sandbox](../components/Sandbox.md) | context | readwrite | init, tick, finish | grass field made in init; species counters, `by_group`, `next_group` kept by every birth, death and newcomer |
| [AgentsReset](../components/AgentsReset.md) | context | write+create | preinit | emplaced when the world has none, so that the host's RESET reaches SandboxObserve |
| [AgentEnvParams](../components/AgentEnvParams.md) | context | read | preinit, tick (applyEnv) | `lifeScale`, `walkerShare`, `rewardScale` over the file's values, at the start of every window |
| [WorldInfo](../components/WorldInfo.md) | context | read | init | the world's seed for the grass field |
| [WorldRng](../components/WorldRng.md) | context | readwrite | init (populate), tick | places, yaws, sexes, ages and mind phases of the animals drawn from it |
| [WorldClock](../components/WorldClock.md) | context | read | tick | `tick % decision_period == 0` is the first tick of a window |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | init, tick, finish | the contact listener set (init) and cleared (finish), four static walls round the arena added and removed, puppets' bodies moved with SetPositionAndRotation every tick |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | write+create | init (populate), tick | spawnSandboxAnimal: a new animal entity with its species, ragdoll group, sex, age, energy and growth stage |
| [AnimalBot](../components/AnimalBot.md) | component | write+create | init (populate), tick | spawnSandboxAnimal: the ragdoll (body of its stage, walker params, ranges, kinematic in puppet worlds, orders on) built and stood at its place |
| [AgentIO](../components/AgentIO.md) | component | write+create | init (populate), tick | spawnSandboxAnimal: the walker's agent on the animal (Animal_<id>, stacked observation, `nogail_order` sensor; not in puppet worlds) and the mind's on a second entity (Action_<id>, 39 floats, 4 actions, a 5-way branch, the `Interests` buffer) |
| [SandboxMind](../components/SandboxMind.md) | component | write+create | init (populate), tick | spawnSandboxAnimal: the mind's entity, linked to its animal, with a random phase in `mind_every` |
| [AgentEnvParams](../components/AgentEnvParams.md) | context | read | init (populate), tick (applyAnimalEnvParams) | the walker's reward and fall parameters overridden by the trainer's environment parameters |
| [TerrainColliders](../components/TerrainColliders.md) | context | read | init (populate), tick (buildAnimalRagdoll) | only whether a std::shared_ptr<TerrainColliders> exists: the ragdoll's bodies get LinearCast motion quality then |
| [SandboxShape](../components/SandboxShape.md) | component | write+create | init (populate), tick (makeShapes, draw) | one entity per collision shape of the ragdoll, linked to its animal, when the world draws (`draw`) |
| [WorldShape](../components/WorldShape.md) | component | write+create | init (populate), tick (makeShapes, draw) | capsule, sphere or box of the shape, its segment and local offset, for WorldView |
| [Transform](../components/Transform.md) | component | write+create | init (populate), tick (makeShapes, draw) | next to each drawn shape, placed every tick by SandboxLife |
| [ModelRef](../components/ModelRef.md) | component | write+create | init (populate), tick (makeShapes, draw) | an empty model with the colour of the animal: brown deer, grey wolves, males darker, the head lighter |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | readwrite | tick | dead and reported: mind dropped; restand: stood up again (grown or stunned); births: gestation ends, +1 objective to both parents; minds' orders |
| [AnimalBot](../components/AnimalBot.md) | component | readwrite | tick | limp on death (zero drives), regrown ragdoll, stood up again (standAnimalBot), mode, head, command from the mind (mindOrders), drives from the walker (driveAnimalBot, weakened by lost health), command eased every tick |
| [AgentIO](../components/AgentIO.md) | component | readwrite | tick | walker's action read for the drives, mind's action and discrete choice read for the orders; restood walkers get done/truncated cleared and zero actions; `heuristic`: the scripted mind writes the action of every mind that asked, clears `requested` and its stats |
| [AgentIO](../components/AgentIO.md) | component | destroy | tick | removed from a dead animal once its last steps went out (reported), with the mind's entity |
| [SandboxMind](../components/SandboxMind.md) | component | read | tick (heuristic) | which animal a requesting mind is for |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | destroy | init (populate), tick, finish (removeAnimal) | the animal's entity is destroyed with its components |
| [AnimalBot](../components/AnimalBot.md) | component | destroy | init (populate), tick, finish (removeAnimal) | destroyAnimalRagdoll removes its Jolt bodies and constraints, then the entity goes |
| [SandboxMind](../components/SandboxMind.md) | component | destroy | init (populate), tick, finish (removeAnimal) | the mind's entity is destroyed with its AgentIO |
| [AgentIO](../components/AgentIO.md) | component | destroy | init (populate), tick, finish (removeAnimal) | both agents leave the world with their entities |
| [SandboxShape](../components/SandboxShape.md) | component | read+destroy | init (populate), tick, finish (removeShapes) | the drawn shapes whose `animal` is the one removed are destroyed (with their Transform, WorldShape, ModelRef, PhysicsPose) |
| [Transform](../components/Transform.md) | component | write+create | init (draw) | one flat box per 5 m square of the arena that is mostly grass |
| [WorldShape](../components/WorldShape.md) | component | write+create | init (draw) | the grass tiles, as boxes 2 cm thick |
| [ModelRef](../components/ModelRef.md) | component | write+create | init (draw) | the grass tiles' colour, darker where the grass is taller |

## Side effects

- Reads resources/agents/params/life.params.json (or `life`), resources/agents/params/animal.params.json, the generated bodies resources/animals/bodies/<id>.body.json and resources/agents/rewards/<id>.rewards.json (hashes kept); throws std::runtime_error on a missing life file, an unknown species or a weapon segment the body lacks.
- Builds a Jolt ragdoll per animal (bodies and constraints in the animal collision layer, group = its ragdoll group), destroys it when the animal leaves or grows; four static wall boxes round the arena.
- Installs SandboxSys::Contacts as the PhysicsSystem's contact listener: it records the estimated impulse of every contact between two ragdolls (from Jolt's threads, under a mutex) for SandboxLife.
- Creates and destroys entities: animals, minds, drawn shapes and grass tiles.
- Logs one debug line per species at init (count, bodies, standing muzzle height, reward terms and round).

## Order and dependencies

- Needs the world's [PhysicsWorld](../components/PhysicsWorld.md) (created on demand by `physicsWorld(reg)`), a
  [WorldRng](../components/WorldRng.md) and a [WorldClock](../components/WorldClock.md), and the generated animal
  bodies (`tools/animal_body.py` / `animal_body.exe`).
- Runs after the Act system of the window (`TrainerActions@act`, which writes the trainer's actions into the
  AgentIO components, or `Policies@act`) so that it reads this window's decisions, and before `PhysicsStep`
  so that the drives and puppet poses apply in the step. [SandboxLife](SandboxLife.md) follows the step and
  [SandboxObserve](SandboxObserve.md) closes the window (template `common/sandbox_world.json`).
- The contact listener it installs is read by SandboxLife; the world's physics must not have another listener.
- Its window work removes dead animals only once SandboxObserve has reported their terminal steps
  (`reported`), so the host always sees an agent's end.

## Used in scenes

<!-- generated -->
- [`common/sandbox_world.json`](../../../resources/scenes/common/sandbox_world.json): `fixed`

## Tests

- `SANDBOX.1` to `SANDBOX.10` in [`tests/unit/sandbox.cpp`](../../../tests/unit/sandbox.cpp) (skipped without the
  deer's and the wolf's bodies): the population and its agents, standing with zero actions, wounds in Attack mode,
  falls, deaths and carcasses, feeding, mating and growth, the mind's observation, the scripted minds, the weapon.
- `LIFE.*` (the rules in `life.cpp`), `EXPR.*` (reward expressions) in the same file.
- `MA.5` in [`tests/protocol/multi_env.cpp`](../../../tests/protocol/multi_env.cpp): `sandbox_train` driven by the
  mock trainer.
