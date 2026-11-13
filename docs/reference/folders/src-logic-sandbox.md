# src/logic/sandbox

The sandbox of [action agents](../../action-agents.md): animals of several species (deer `deer_red` and wolves
`wolf_2` by default, men with a spear in `life_hunters.params.json`) that live in one square arena, eat, fight, mate,
grow and die. Each animal is a ragdoll (`AnimalBot`, from `src/logic/animals/`) with two agents: its walker
(`Animal_<id>`, the joints, every window) and its mind (`Action_<id>`: speed, turn, head and a mode, every
`mind_every` windows). The rules of life (energy, health, wounds, grass, growth) and the reward files of the minds
(expressions over signals, rewritten by the LLM between rounds) are here too. Everything runs in a world's
registry (`fixed` lane of `common/sandbox_world.json`).

## Files

| File | What |
|---|---|
| [`sandbox.h`](../../../src/logic/sandbox/sandbox.h) | `Sandbox` (world context), `SandboxAnimal`, `SandboxMind`, `SandboxShape`, `SandboxSpecies`, `RewardProgram`, `SandboxSignal` (the 40 signals), `MindObs` (the mind's observation layout), the system classes, `spawnSandboxAnimal`, `observeSandboxMind`, `sandboxSignals`, `sandboxHeuristic` |
| [`sandbox.cpp`](../../../src/logic/sandbox/sandbox.cpp) | systems Sandbox, SandboxLife, SandboxObserve, SandboxHeuristic; the contact listener (`SandboxSys::Contacts`), population, births, deaths, the minds' orders, the drawn shapes, `registerSandbox` |
| [`life.h`](../../../src/logic/sandbox/life.h), [`life.cpp`](../../../src/logic/sandbox/life.cpp) | the rules without a world: `SpeciesLife`, `GrassParams`, `LifeParams` (parse / load of `life.params.json`), `energyDrain`, `woundOf`, `GrassField` (grid of grass heights eaten and growing back, from `GrassHeightField` noise), `scaleAnimalBody`, `armAnimalBody` |
| [`expr.h`](../../../src/logic/sandbox/expr.h), [`expr.cpp`](../../../src/logic/sandbox/expr.cpp) | `Expression` (numbers, named signals, arithmetic, comparisons, min / max / abs / exp / log / sqrt / pow / sign / step / clamp / if, compiled to postfix, non finite results as 0), `ExprError` with the position of the fault |

## Systems

In the order of the template's `fixed` list (`RigidBody`, `TrainerActions@act`, Sandbox, `PhysicsStep`, SandboxLife,
SandboxObserve@observe):

- [Sandbox](../systems/Sandbox.md): the context, the population; at each window's start carcasses leave, the fallen
  stand, births, newcomers, minds' orders and walkers' drives
- [SandboxLife](../systems/SandboxLife.md): after the step, falls, energy, feeding, wounds, mating, deaths, grass
- [SandboxObserve](../systems/SandboxObserve.md): RESET, the walkers' and the due minds' rewards and observations
- [SandboxHeuristic](../systems/SandboxHeuristic.md): the scripted minds as an Act system (unused by the scenes,
  which use Sandbox's `heuristic` parameter)

## Components and context variables

- [Sandbox](../components/Sandbox.md): world context: rules, species, grass, counters
- [SandboxAnimal](../components/SandboxAnimal.md): an animal's life and its agents' windows
- [SandboxMind](../components/SandboxMind.md): the entity of an animal's mind (with its AgentIO)
- [SandboxShape](../components/SandboxShape.md): a drawn body of an animal (`draw`)

Used from elsewhere: [AnimalBot](../components/AnimalBot.md) (as a component here), [AgentIO](../components/AgentIO.md)
(components), [AgentsReset](../components/AgentsReset.md), [AgentEnvParams](../components/AgentEnvParams.md),
[WorldShape](../components/WorldShape.md), [PhysicsPose](../components/PhysicsPose.md).

## Worth knowing

- **Two agents per animal**: the walker's AgentIO is on the animal's entity, the mind's on a second entity
  (`SandboxAnimal::mind`). A dead animal sends its terminal steps (`reported`), then loses both agents at the next
  window and stays a carcass until eaten or rotten.
- **Signals and rewards**: `sandboxSignals` fills `SandboxSignal::count` values over the mind's last decision;
  `resources/agents/rewards/<id>.rewards.json` holds named terms (`RewardProgram::parse`, `Expression::parse`
  against `sandboxSignalNames()`), summed with the objective (birth +1 each parent, early death -1).
  `TrainingEnv.exe mode=signals` prints the names; `tools/sandbox_round.py` runs the rounds.
- **Puppet worlds** (`puppet`): kinematic bodies in their standing pose slid along their command, no walkers, a
  muzzle held on a trunk wounds: a check of the rules and the minds.
- **Wounds** come from Jolt: `SandboxSys::Contacts` records the estimated contact impulse between two ragdoll groups
  (body user data: group in the high 32 bits, segment in the low), a head or a weapon in Attack mode wounds by
  impulse over the victim's mass (`woundOf`).
- Durations are seconds at a life scale of 1; `lifeScale`, `walkerShare`, `rewardScale` are environment parameters
  read at every window.
- Observations: `MindObs` (39 floats + the `Interests` buffer of 12-float rows, seen within the field of view round
  where the head points, or heard moving close by).

## Dependencies

- `src/logic/animals/` (`AnimalBot`, `buildAnimalRagdoll`, `standAnimalBot`, `driveAnimalBot`, `syncAnimalBot`,
  `rateAnimal`, `observeAnimal`, `observeOrder`, bodies and their scaling), `src/logic/agents/` (`AgentIO`,
  `AgentsReset`, `AgentEnvParams`, `HeadingFrame`, `WorldShape`, `fileSha256`), `src/physics/` (`PhysicsWorld`,
  `PhysicsPose`), `src/logic/grass/` (`GrassHeightField` for the grass field's noise), `src/scene/` (`WorldClock`,
  `WorldRng`, `WorldInfo`, `registerSystem`), `src/util/` (`libraryResourceFile`, log).
- Used by `src/train/sandbox_host.cpp` (mode=eval, mode=signals), `src/view/sandbox_watch.cpp` (the viewer's
  camera), the system registry and `tests/unit/sandbox.cpp`.

## Design docs

- [docs/action-agents.md](../../action-agents.md) (decisions, rules of life, rewards, inference)
- [docs/physical-agents/reports/animal-agents.md](../../physical-agents/reports/animal-agents.md) (the walkers)
