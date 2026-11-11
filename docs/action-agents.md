# Action agents

Animals (and later humans) that decide what to do: where to go, when to feed, attack, mate or rest. Their objective
is to survive and reproduce. Every species has its own policies, and all of them learn in the same sandbox: wolves
kill deer, deer have to graze. The first sandbox has red deer and wolves.

Source: Unity Dynamical's `Assets/AI/ActionBot/Scripts/` (`ActionAgent.cs`, `Predator.cs`, `Prey.cs`,
`InterestSensor.cs`). What is kept from it:

- **The split between a command and what the body achieves.** `ActionAgent` outputs target poses, a PD body follows.
  Here the action policy outputs a walk command, a head offset and a mode; the physical walker follows.
- **Scenario rules outside the agent.** `Predator` and `Prey` add rewards and kills through the agent's events. Here
  the rules of life are systems of the world; the agent only senses and acts.
- **The interest buffer.** `InterestSensor` lists the other animals in the agent's frame in a buffer sensor (variable
  length, attention in the trainer).

What is not kept: target poses of head and hands, the VR recordings and their GAIL split (`nogail` sensors), the
duel's opponent block.

## Decisions (2026-11-11)

| Question | Decision |
|---|---|
| What the action policy commands | Speed, turn, a head rotation offset (yaw, pitch) and a mode, on the **physical ragdoll** |
| Modes | Move, Feed, Attack, Mate, Rest (one discrete branch) |
| First species | Red deer (`deer_red`) and wolves (`wolf_2`) |
| Policies | Per species, shared by all its animals; one `mlagents-learn` run, several behaviours in one environment |
| Networks per species | Two: the action policy and the walker. **Both learn together** in the sandbox |
| Walker's reward in the sandbox | Its own (command, mode, GAIL) plus a share of the action policy's reward |
| Where the walkers start | Pretrained alone on flat ground, then they keep learning in the sandbox |
| Alive or dead | An energy store and health |
| Kill | **Physical contact**: the impulse of the head's body on the other animal, in Attack mode |
| Grazing | The muzzle in grass that gets shorter and grows back |
| Births | Two adults of opposite sex, both in Mate mode, a gestation, a young near its mother |
| Sex and age | One body per species; sex is a flag; the young are the same body, smaller, growing |
| Senses | Interest buffer with **line of sight** (field of view, trunks), hearing close by, grass on a ring |
| Land | A flat arena of about 200 m with grass patches; the terrain later |
| Population per world | 12 deer and 4 wolves at the start, at most 24 and 8 |
| Episode | A life: from birth (or spawn) to death. A species under its minimum gets animals at the edge |
| Time scale | A curriculum: minutes at first, every duration stretched by an environment parameter |
| Objective | A birth is +1 for each parent; a death before old age is -1 |
| Intermediate rewards | Written by the LLM between rounds: expressions over named signals, one file per species |
| Humans | After deer and wolves, as hunters on the HumanBot walker with a spear |

## One animal, two agents

```
            5 Hz                                  30 Hz                       60 Hz
senses ─► action policy ─► speed, turn        ─► walker ─► joint drives ─► ragdoll (Jolt)
          Action_<species>  head yaw, pitch       Animal_<species>             │
                            mode                                               ▼
            ▲                                                        contacts, muzzle, work
            └──────────────── energy, health, births, deaths ◄── rules of life ┘
```

**Action policy** (`Action_<species>`), a decision every 6 windows of the walker (5 Hz):

- Observations, three sensors:
  - *self* (vector): energy, health, age, sex, pregnancy, size, the last mode, its own velocity in the heading frame,
    the head offset achieved, seconds since it last fed.
  - *interests* (buffer, up to 16 rows): for each animal it perceives, position and velocity in its heading frame,
    kind (own species, prey, predator, carcass), sex, size, whether it is in Mate mode. Perceived: inside the field of
    view around where the **head** points (wide for deer, narrow for wolves) with no trunk between, or close and
    moving (hearing: direction and a rough distance, no kind).
  - *grass* (vector): grass heights on two rings around it, in its heading frame.
- Actions: 4 continuous (speed, turn, head yaw offset, head pitch offset) and one discrete branch of 5 (the mode).

**Walker** (`Animal_<species>`, as today, 30 Hz): the observation's header gains the head offset asked for and the
mode; actions are still 8 per joint. Its reward:

- the command (today's `root_reward`), in every mode;
- the head: its direction against the offset asked for;
- the mode: Feed, the muzzle down to the ground or the carcass in front; Attack, the head's speed along its
  direction; Rest, low work; Mate, standing still by the partner;
- GAIL on the procedural walk (recorded in Move mode with a zero offset), weaker than in pretraining;
- `walker_share` times the reward the action policy got over the same time.

Faster gaits than the demonstrated walk come from the shared reward, not from demonstrations.

The pretraining (`tools/train_animal.py`) uses the same observation layout: modes and head offsets are drawn at
random there, so a pretrained walker loads in the sandbox unchanged.

## Rules of life

All in parameters (`resources/agents/params/life.params.json`, per species blocks); `life_scale` (environment
parameter) multiplies every duration.

- **Energy**: goes down by a basal rate plus the positive mechanical work of the drives (so running costs). Feeding
  brings it back. At zero the animal dies (starved).
- **Health**: goes down with wounds, comes back slowly, faster in Rest mode. At zero the animal dies (killed). A
  wounded animal's drives are weaker.
- **Wounds**: Jolt's contact impulse between the head's body of an animal in Attack mode and a body of another
  animal, over a threshold, scaled by the victim's mass. A bump while walking does not wound. Deer can wound too.
- **Grazing**: Feed mode, the muzzle within reach of the ground where the grass field has grass, moving slowly:
  energy flows in and the grass gets shorter there. The grass field is a grid of 1 m cells, started from
  `GrassHeightField` and the soil patches (bare ground has none); it grows back over minutes.
- **Carcass**: a dead animal's ragdoll goes limp and stays, with the meat its mass gives. Wolves feed on it as deer
  do on grass (muzzle on a body of the carcass, Feed mode). It leaves when eaten or rotten.
- **Mating**: a male and a female adult, both in Mate mode, trunks within a body length for a few seconds, both
  with enough energy. The female is pregnant; after the gestation the young is born near her and she pays energy.
- **Growth**: the young is the species' body at half size, rebuilt at three quarters and full size as it ages.
  Adult at full size.
- **Old age**: the life ends without penalty (a truncation for the trainer).
- **Respawn**: a species under its minimum gets adults at the edge of the arena, so training never stalls.

Time scale at `life_scale` 1: a deer starves in about 3 minutes without grass, a wolf in 6 without meat; adult at 2
minutes, gestation 1 minute, old age around 20 minutes.

## Rewards

The objective is fixed in the engine: +1 to each parent at a birth, -1 at a death by hunger or wounds.

The intermediate rewards are data, written by the LLM (Claude, in a session) between rounds:

```json
// resources/agents/rewards/deer_red.rewards.json
{ "round": 3, "notes": "round 2: deer starve next to grass, never lower the head while a wolf is in sight",
  "terms": [
    { "name": "eat",   "expr": "0.2 * energy_gain" },
    { "name": "fear",  "expr": "-0.02 * max(0, 1 - predator_distance / 15)" },
    { "name": "mate",  "expr": "0.05 * step(energy - 0.6) * adult * max(0, 1 - mate_distance / 10)" } ],
  "scale": 1.0 }
```

- Expressions: numbers, signals, `+ - * /`, comparisons, `min max abs clamp exp log sqrt step if`. A small evaluator
  in the engine; a file that does not parse stops the run at start.
- Signals are published by the rules of life for each animal over the action decision that just ended: `energy`,
  `energy_gain`, `energy_spent`, `health`, `damage_taken`, `damage_dealt`, `speed`, `mode_*`, `predator_distance`,
  `prey_distance`, `mate_distance`, `carcass_distance`, `grass_here`, `herd_distance`, `age`, `adult`, `pregnant`,
  `young_alive`... (the list is printed by `TrainingEnv.exe mode=signals`).
- Every term is a stat of its own (`Rewards/<species>/<term>`), next to `Life/<species>/Lifetime`, `Births`,
  `Deaths starved`, `Deaths killed`, `Kills`, `Energy`.
- `scale` multiplies all the terms: an environment parameter can anneal it to zero so the last policies are judged
  on the objective alone.

A round (`tools/sandbox_round.py`): train for N steps from the last round's checkpoints, write
`build/runs/<run>/round_<k>.json` (the stats above, per species), stop. The LLM reads it, writes the next reward
files (the old ones are kept in the run folder) and starts the next round. No API loop: a session does it.

## Engine changes

1. **Training host** (M1, built, see below): several agents in a world, several behaviours, agents that are born and
   die, hybrid actions, buffer sensors, agents that decide at their own rate.
2. **Many animals in a world**: `AnimalBot` is a world context variable (one animal per world). It becomes a
   component; ragdolls of several animals share one `PhysicsWorld`, with collision groups so an animal's own bodies
   keep their rules and animals collide with each other.
3. **Walker**: header with head offset and mode, the mode terms, the share of the action reward.
4. **Life**: `Life` component, grass field, contact listener for wounds, carcasses, mating, growth, respawn.
5. **Action agent**: senses, the reward file and its evaluator.
6. **Inference**: `Policy` runs one ONNX per behaviour over all its agents (viewer, eval, herds).

## Several agents in a world (M1)

`AgentIO` (`logic/agents/agent_io.h`) was the one agent of a world, a context variable. It can now also be a
**component**: every entity that has one is an agent, next to the world's own if it has one.

- `branches`, `discrete`: discrete action branches next to the continuous actions (hybrid actions).
- `sensors`: further observations after the vector one: vectors, or buffers (`rows` x `size`, `count` rows filled,
  the rest zero: ML-Agents' variable length observation).
- `requested`: the agent asks for a decision with this observation. Its systems set it at the Observe phase when
  they want one; the host sends the agent and clears the flag. An agent that is not sent keeps its last action.
  The world's own `AgentIO` asks every window, as before.
- `done`: sent whatever `requested` says. After a terminal step the entity either starts a new episode (same id) or
  is destroyed by its systems at the next window; a new entity gets a new id.
- `id`: given by the host, unique per run.

`WorldsEnvironment` gathers the agents of every world at each exchange, grouped by behaviour. A behaviour's brain
parameters go out with the first output that has one of its agents. All agents of a behaviour must have the same
layout. Worlds are stepped until at least one agent asks for a decision or ends (as Unity, which only talks to the
trainer then). Stats are averaged per key over the agents that report it: keys carry the species.

Fixture: scene `pursuit` (`logic/agents/pursuit.{h,cpp}`), Unity's Predator and Prey as points on a square field, no
physics. Chasers (behaviour `Chaser`, a decision every window) catch runners within a radius; runners (`Runner`, a
decision every second window) die when caught and another is born elsewhere, with a new id. Both have 2 continuous
actions (speed, turn), a discrete branch (walk or sprint, on a stamina), a vector observation and a buffer of the
others. `training/pursuit_ppo.yaml`, `tools/train_sphere.py --scene pursuit`. Tests `PURSUIT.*`
(`tests/unit/pursuit.cpp`) and `MA.*` (`tests/protocol/multi_env.cpp`, the mock trainer).

First run (`build/runs/pursuit_m1`, 2 environments of 16 worlds, 600k steps per behaviour, 10 minutes next to
another training): the trainer takes both behaviours, their hybrid actions and buffers, the births and deaths, and
exports `Chaser-*.onnx` and `Runner-*.onnx`. What it learned is thin: the share of runners that end caught goes
from 0.24 (160k) to about 0.15 (from 320k on) and the runners' return from 1.57 to 1.73 of a possible 2; the
chasers' return goes down with it (0.44 to 0.22 catches an episode) and their entropy hardly moves. So the run
checks the host, not that chasers learn to chase: a longer run, or chasers against runners that stand still, is
what would check the fixture's rewards and actions end to end.

## Milestones

| | What | Check |
|---|---|---|
| M1 | Several agents and behaviours in a world through the training host; `pursuit` | `MA.*`, `PURSUIT.*` pass; `mlagents-learn` runs both behaviours to the end (learning not shown yet) |
| M2 | `AnimalBot` as a component; scene `sandbox_stand`: 12 deer and 4 wolves standing in one world | `AA.*` still pass; bench of a full world |
| M3 | Walker with head offset and mode; pretraining of `deer_red` and `wolf_2` with them, speeds up to a run | eval per mode; the head reaches the ground in Feed |
| M4 | Rules of life, with a scripted action heuristic (graze, chase, mate) instead of a policy | `LIFE.*`: energy books, a kill by contact, a birth, grass eaten and back |
| M5 | Action agent: senses, reward file and evaluator, `sandbox_train`, `sandbox_view`, `tools/sandbox_round.py` | first round: four behaviours train, stats per species |
| M6 | Rounds: reward files by the LLM, `life_scale` curriculum, report | lifetimes and births per round |
| M7 | Inference in the viewer and in `forest_herds`; videos | |
| M8 | Humans as hunters | |
| later | The terrain; the other herd kinds (reindeer, bison, horses, boars, lions, bears...) | |

## Risks

- **Four networks learning under each other.** The walker's command comes from a policy that changes, the action
  policy's body from a walker that changes. Mitigations: pretrained walkers, a slower learning rate for them in the
  sandbox, GAIL kept on, `walker_share` raised from zero.
- **The walkers.** The red deer's 20M walker shuffles; the wolf has none. Kills by contact need a wolf that can
  catch a deer and strike with its head. M3 decides whether this holds.
- **Cost.** A full world is 32 ragdolls of about 15 bodies and 64 agents. M2's bench gives the steps per second;
  the population or the number of worlds follows from it.
- **Sparse objective.** A birth needs minutes of the right behaviour from two animals. The time scale starts short
  and the intermediate rewards carry the first rounds.
