# SandboxObserve

`SandboxObserveSys` in [`src/logic/sandbox/sandbox.cpp`](../../../src/logic/sandbox/sandbox.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, `SandboxObserve@observe` (last tick of each decision window) · Parameters: no

The Observe phase of the [sandbox](Sandbox.md): it answers the trainer's RESET, then fills every agent component
the host will read. Each walker gets its reward, its end and its observation every window; each mind that is due
(or whose animal just died) gets its reward (the objective plus the species' reward file), its end, its stats and
its observation. Both ask for a decision (`AgentIO::requested`).

## Behaviour

**tick** (also called with h = 0 by `SystemSet::fixed_observe_only` when the host resets):

1. A pending RESET ([AgentsReset](../components/AgentsReset.md)`::requested`, set by
   [WorldsEnvironment](service-WorldsEnvironment.md)): cleared, the grass field made again from the world's seed,
   the population drawn again (`populate`: every animal removed, `start` of each species spawned), `seconds` = 0.
2. `windows++`. Window length = decision period x dt; the walker's episode = `walker_episode` seconds in ticks.
3. For every animal in entity order that is alive or dead but not yet reported:
   - **Mind** (if it still has one), when it is fresh, due (`(windows + phase) % mind_every == 0`) or the animal
     died: a fresh mind starts with reward 0. Otherwise `sandboxSignals` fills the 40 signals of the decision
     (state, window sums, distances and closings to the nearest predator, prey, mate, carcass, herd and grass,
     herd count, ...), each term of the species' `RewardProgram` is evaluated on them times `reward_scale` x the
     file's `scale`; reward = objective + shaped terms. Stats: `Rewards/<id>/<term>`, `Objective`, `Shaped`,
     `Life/<id>/Energy|Health|Feeding|Speed|Population`, `Mode/<id>/<mode>`, summed events (Births, Conceptions,
     Kills, Falls, Damage dealt), and on a death Lifetime, Offspring, Return, Died starved/killed/old, Deaths;
     the first mind of the window also writes `Sandbox/Grass standing` and `Sandbox/Life scale`. `done` on a death,
     `truncated` on death by old age. The walker's share (`walker_share` x reward) is kept, the objective cleared.
     Then the window restarts from the trunk's position, `observeSandboxMind` writes the 39 floats (itself, head
     offsets, speeds in its heading frame, the arena's middle, mode one-hot, grass heights at 8 directions 3 m and
     10 m away) and the `Interests` rows (the others seen in its field of view round where the head points, or
     heard moving within `hearing`, nearest first), and `requested` is set.
   - **Walker** (physical worlds, while it has its AgentIO): a fresh episode starts with reward 0; otherwise
     reward = (life reward + root, head and mode ratings of `rateAnimal` over the window) x reward factor, or the
     death penalty on a fall, plus its share of the mind's reward. `done` on a fall, a death, a growth or the end
     of its episode; `truncated` unless a fall or a death not of old age. Stats `Walker/<id>/Root Reward`, `Order
     Reward`, `Head Error`, `Commanded Speed`, `Speed Ratio`, `Fell`. A done walker that lives is stood up at the
     next window (fall, growth) or starts a new episode where it is (time). Then snapshots of trunk, command, mode
     and head, the two stacked frames (`observeAnimal`, non finite values as 0) and the order sensor
     (`observeOrder`), `requested` set.
   - Puppet worlds: only the trunk snapshot, and a due growth makes it stand again.
   - A dead animal is marked `reported`: its agents' terminal steps went out with this window.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Sandbox](../components/Sandbox.md) | context | readwrite | tick | `windows` counted; on a RESET the grass made again, the animals again (populate), `seconds` 0; species reward programs read |
| [WorldClock](../components/WorldClock.md) | context | read | tick | window length and the walker's episode length in ticks |
| [AgentsReset](../components/AgentsReset.md) | context | readwrite | tick | a pending RESET is consumed (requested back to false) |
| [WorldInfo](../components/WorldInfo.md) | context | read | tick (reset) | seed of the new grass field |
| [WorldRng](../components/WorldRng.md) | context | readwrite | tick (reset: populate) | draws of the new population |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | tick (reset) | the old ragdolls destroyed, new ones built and stood |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | readwrite | tick | signals of the decision (the nearest distances kept for the closings), objective paid and cleared, window reset, lifetime reward, `share`, `walker_ticks`, `walker_fresh`, `restand`, `fell` cleared, `reported` set on a death |
| [AnimalBot](../components/AnimalBot.md) | component | readwrite | tick | rateAnimal over the window, snapshots of trunk, command, mode and head, `previous_frame`, `killed` cleared |
| [SandboxMind](../components/SandboxMind.md) | component | readwrite | tick | `fresh` cleared at its first decision, `phase` read for when it is due |
| [AgentIO](../components/AgentIO.md) | component | readwrite | tick | mind: reward (objective + the reward file's terms), done, truncated, stats, observation and `Interests` rows (observeSandboxMind), requested; walker: reward (rateAnimal, a fall's penalty, its share of the mind's), done, truncated, stats, stacked observation and order sensor, requested |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | write+create | tick (reset: populate) | spawnSandboxAnimal: a new animal entity with its species, ragdoll group, sex, age, energy and growth stage |
| [AnimalBot](../components/AnimalBot.md) | component | write+create | tick (reset: populate) | spawnSandboxAnimal: the ragdoll (body of its stage, walker params, ranges, kinematic in puppet worlds, orders on) built and stood at its place |
| [AgentIO](../components/AgentIO.md) | component | write+create | tick (reset: populate) | spawnSandboxAnimal: the walker's agent on the animal (Animal_<id>, stacked observation, `nogail_order` sensor; not in puppet worlds) and the mind's on a second entity (Action_<id>, 39 floats, 4 actions, a 5-way branch, the `Interests` buffer) |
| [SandboxMind](../components/SandboxMind.md) | component | write+create | tick (reset: populate) | spawnSandboxAnimal: the mind's entity, linked to its animal, with a random phase in `mind_every` |
| [AgentEnvParams](../components/AgentEnvParams.md) | context | read | tick (reset: populate) (applyAnimalEnvParams) | the walker's reward and fall parameters overridden by the trainer's environment parameters |
| [TerrainColliders](../components/TerrainColliders.md) | context | read | tick (reset: populate) (buildAnimalRagdoll) | only whether a std::shared_ptr<TerrainColliders> exists: the ragdoll's bodies get LinearCast motion quality then |
| [SandboxShape](../components/SandboxShape.md) | component | write+create | tick (reset: populate) (makeShapes, draw) | one entity per collision shape of the ragdoll, linked to its animal, when the world draws (`draw`) |
| [WorldShape](../components/WorldShape.md) | component | write+create | tick (reset: populate) (makeShapes, draw) | capsule, sphere or box of the shape, its segment and local offset, for WorldView |
| [Transform](../components/Transform.md) | component | write+create | tick (reset: populate) (makeShapes, draw) | next to each drawn shape, placed every tick by SandboxLife |
| [ModelRef](../components/ModelRef.md) | component | write+create | tick (reset: populate) (makeShapes, draw) | an empty model with the colour of the animal: brown deer, grey wolves, males darker, the head lighter |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | destroy | tick (reset: populate) (removeAnimal) | the animal's entity is destroyed with its components |
| [AnimalBot](../components/AnimalBot.md) | component | destroy | tick (reset: populate) (removeAnimal) | destroyAnimalRagdoll removes its Jolt bodies and constraints, then the entity goes |
| [SandboxMind](../components/SandboxMind.md) | component | destroy | tick (reset: populate) (removeAnimal) | the mind's entity is destroyed with its AgentIO |
| [AgentIO](../components/AgentIO.md) | component | destroy | tick (reset: populate) (removeAnimal) | both agents leave the world with their entities |
| [SandboxShape](../components/SandboxShape.md) | component | read+destroy | tick (reset: populate) (removeShapes) | the drawn shapes whose `animal` is the one removed are destroyed (with their Transform, WorldShape, ModelRef, PhysicsPose) |

## Side effects

- On a RESET: every animal and its ragdoll destroyed and the population drawn again (entities and Jolt bodies created and destroyed).

## Order and dependencies

- Last of the world's fixed systems, on the Observe tick of each window, after [SandboxLife](SandboxLife.md)
  (falls, deaths, feeding of the window). The host (WorldsEnvironment, Policies, the scripted minds) reads the
  agents right after it.
- Reads the reward programs [Sandbox](Sandbox.md) loaded in preinit.

## Used in scenes
<!-- generated -->

## Tests

- `SANDBOX.1`, `SANDBOX.4`, `SANDBOX.5`, `SANDBOX.7`, `SANDBOX.8` (the mind's observation and rows) in
  [`tests/unit/sandbox.cpp`](../../../tests/unit/sandbox.cpp); `EXPR.3` (the repository's reward files parse
  against the signals).
- `MA.5` in [`tests/protocol/multi_env.cpp`](../../../tests/protocol/multi_env.cpp): walkers every window, minds
  every sixth.
