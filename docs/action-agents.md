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

Decided while building (2026-11-11, "do it all non stop": no question asked, each can be reversed in a parameter):

| Question | Decision |
|---|---|
| A walker that falls | Its episode ends (as when it trains alone), not the life: the animal loses health (`fall_damage`), is stood up where it fell and holds still for `stun` seconds |
| The order's place in the walker's observation | A second sensor, `nogail_order` (head yaw, head pitch, the mode as a one-hot): the fork's GAIL does not see it, so the demonstrations of the procedural walk stay valid and the main observation is unchanged |
| What a blow weighs | Jolt's estimate of the contact impulse between the two bodies that meet (a head of 10 kg on a leg or a trunk: 15 to 50 N s at 5 m/s), over the victim's whole mass. `wound_threshold` 0.01 m/s, `kill_impulse` 0.25 m/s: the head thrown at 5 m/s on a deer's leg takes 8% of its health |
| What moving costs | The positive changes of the bodies' mechanical energy (kinetic and potential), smoothed over a quarter of a second; at 0.1 m g v watts the store goes down twice as fast as standing |
| Trees | None in the first arena: sight is a field of view around the head and a range, nothing hides |
| Puppets | A mode of the sandbox for checking the rules and the minds without walkers (`puppet`): kinematic bodies slid along their commands. Not the product: what a body does is not learned there |
| A species' steps in a round | In proportion to its animals (4 wolves decide a third as often as 12 deer): every behaviour ends the round together |

## One animal, two agents

```
            5 Hz                                  30 Hz                       60 Hz
senses ─► mind ─► speed, turn              ─► walker ─► joint drives ─► ragdoll (Jolt)
          Action_<species>  head yaw, pitch    Animal_<species>             │
                            mode                                            ▼
            ▲                                                     contacts, muzzle, work
            └──────────── energy, health, births, deaths ◄── rules of life ┘
```

Both are `AgentIO` components: the animal's entity holds the walker's (next to its `AnimalBot`, the ragdoll, and its
`SandboxAnimal`, the life), a second entity holds the mind's (`SandboxMind`). `logic/sandbox/sandbox.{h,cpp}`.

**Mind** (`Action_<species>`), a decision every 6 windows of the walker (5 a second), each animal on its own phase:

- Observation (`MindObs`), a vector of 39: energy, health, age, adult, sex, pregnant, gestation, size, time since it
  fed, its velocity in its heading frame, where its head points (as the offsets that would ask for it), the height
  of its muzzle, the grass under it, stunned, the middle of the arena in its frame, its mode, and the grass at 8
  directions 3 m and 10 m away.
- Buffer `Interests`, 16 rows of 12, the nearest first: position and relative velocity in its heading frame, own
  species / prey / predator / carcass, sex, size, a possible mate (and whether it is in Mate mode), seen or heard. **Seen**: within `sight` and inside
  the field of view (`fov`: 300 degrees for a deer, 180 for a wolf) around where the **head** points. **Heard**:
  moving, within `hearing`, whatever the direction: a direction and a rough distance, nothing about what it is.
- Actions: speed (under zero stands still: a mind that knows nothing stands half the time; 1 is `top_speed` walk
  speeds), turn (1 is a turn of `turn_radius` lengths), head yaw (to 1 rad either side) and pitch (1.1 rad down, 0.5
  up), and the mode. Mate and Rest stand still, Feed goes slowly. The command turns from where the animal heads: when
  the walker did not follow by more than 0.5 rad the command is put back on the trunk's heading.

**Walker** (`Animal_<species>`, 30 a second): the observation it trains alone with (`observeAnimal`, two stacked
frames) and the order sensor; 8 actions a joint. Its reward (`rateAnimal`):

- the command (`root_reward`), over the speed asked but not under a quarter of the walk speed (a command that all
  but stands would make a step back cost more than a walk earns);
- the head: `head_reward` x exp(-error² / 2 x 0.35²), the angle between where it points and where it is asked to;
- the mode, `mode_reward` times: Feed, exp(-muzzle height / a quarter of the standing height); Attack, the head's
  speed along where it points, up to two walk speeds; Mate, standing still; Rest, every body still;
- GAIL on the procedural walk;
- `walker_share` times what its mind was just paid.

The drives of a wounded animal are weaker (`weak` + (1 - `weak`) x health of their stiffness and torque).

Where the walkers start: `tools/train_animal.py --species <id> --ranges sandbox --scene animal_orders_train
--demo-var orders=true --config training/animal_orders_ppo.yaml --prefix orders`. The animal alone
(`AnimalSpawn.orders`), on the sandbox's physics settings (`common/animal_orders_world.json`), commands from
standing to 2.6 walk speeds (`ranges/sandbox.json`), a mode and head offsets drawn every 2 to 6 seconds (Move 45%,
Feed 17%, Attack 18%, Mate 10%, Rest 10%).

## Rules of life

`logic/sandbox/life.{h,cpp}`, `resources/agents/params/life.params.json` (a `defaults` block, then one per species).
Durations are seconds at a life scale of 1; the environment parameter `lifeScale` multiplies them all.

- **Energy**: a full store lasts `starve` seconds standing (deer 180, wolves 360), less with work, 0.6 of the basal
  rate in Rest mode. At zero the animal dies, starved.
- **Health**: back to full in `heal` seconds, three times faster in Rest mode. At zero the animal dies, killed.
- **Wounds**: a contact between the **head** of an animal in Attack mode (or the body that holds its weapon) and a
  body of another living animal (`SandboxSys::Contacts`, a Jolt contact listener; ragdolls of the sandbox are on
  `PhysicsLayers::animal_layer` and each has its collision group). `bite`: 1 for a wolf, 0.3 for a deer. Whoever
  wounded an animal in its last 10 seconds gets the kill.
- **Grazing**: Feed mode, the trunk slower than `feed_speed`, the muzzle no higher than the grass under it: the
  store fills in `feed` seconds and the cell is eaten at `graze` m a second, down to a stubble. `GrassField`: cells
  of 1 m, patches over 55% of the arena (the engine's `GrassHeightField` for how tall, 0.55 to 1 m, another draw of
  it for where), back to full height in `regrow` seconds.
- **Carcass**: the dead animal's ragdoll goes limp and stays, with `meat` full stores of whoever eats it (times its
  size cubed). A predator in Feed mode with its muzzle within reach of one of its bodies feeds. It leaves when eaten
  or after `rot` seconds.
- **Mating**: a female and a male, adults, both in Mate mode with `mate_energy`, trunks within `mate_reach` lengths
  for `mate_time` seconds. After `gestation` the young is born beside its mother, who pays `birth_cost`.
- **Growth**: born at `young_scale` (0.6) of the species' body (`scaleAnimalBody`: masses by the cube, drives by the
  fourth power, speeds by the square root), rebuilt half way to full size at half the adult age, full size at
  `adult`. The walker's episode is cut when the body changes.
- **Old age** at `old`: the life ends without penalty (a truncation for the trainer).
- **Respawn**: a species under its `min` gets adults at the edge of the arena. No birth over its `max`.
- Walls stand around the arena (`arena`: its half side, 100 m).

## Rewards

The objective is fixed in the engine: +1 to each parent at a birth, -1 at a death by hunger or wounds.

The intermediate rewards are data: `resources/agents/rewards/<species>.rewards.json`, written between rounds.

```json
{ "round": 2,
  "notes": "Round 1: every deer starved. They fed 2% of the time at first and 7% at the end ...",
  "terms": [
    { "name": "eat",   "expr": "2 * energy_gain" },
    { "name": "graze", "expr": "0.004 * step(0.9 - energy) * mode_feed * step(grass_here - 0.1) * max(0, 1 - muzzle_height)" },
    { "name": "fear",  "expr": "-0.01 * max(0, 1 - predator_distance / 15)" } ],
  "scale": 1.0 }
```

- Expressions (`logic/sandbox/expr.{h,cpp}`): numbers, signals, `+ - * /`, comparisons, `min max abs exp log sqrt pow
  sign step clamp if`. A file that does not parse stops the world at its start with the term and the character.
  What is not a number (a division by zero) is worth 0.
- Signals (`SandboxSignal`, `TrainingEnv.exe mode=signals` prints them), over the mind's decision that just ended:
  `energy health age adult male pregnant`, `energy_gain energy_spent damage_taken damage_dealt` (in full stores and
  healths), `kills births falls conceptions`, `speed` (walk speeds), `mode_move mode_feed mode_attack mode_mate
  mode_rest` and `feeding` (parts of the decision), `grass_here grass_near muzzle_height`, `predator_distance
  prey_distance mate_distance carcass_distance herd_distance` (m, 1000 without one; whatever the direction),
  `herd_count since_fed lived dt`.
- Stats, per species: `Rewards/<id>/<term>`, `Objective`, `Shaped`; `Life/<id>/Energy`, `Health`, `Feeding`,
  `Speed`, `Population`, and summed over a summary period `Births`, `Conceptions`, `Kills`, `Falls`, `Deaths`,
  `Damage dealt`; at a death `Lifetime` (seconds in the world), `Offspring`, `Return`, `Died starved / killed / old`;
  `Mode/<id>/<mode>`; `Walker/<id>/Root Reward`, `Order Reward`, `Head Error`, `Speed Ratio`, `Fell`.
- `rewardScale` (environment parameter) multiplies the terms, not the objective.

**A round** (`tools/sandbox_round.py --name <name> --round <k> --steps <n>`): freezes the engine with the life
params and the reward files as they stand (`<name>_r<k>_env`), makes the trainer's config from
`training/sandbox_ppo.yaml`, starts from round k-1 (`--initialize-from`; in round 1 the walkers start from their own
runs), trains, and writes `<name>_r<k>.round.json`: per behaviour the return and the entropy, per species every stat
above, over the first quarter of the round and over its last. Reading it and writing the next files is the LLM's
part, done in a session; the notes of each file say what the last round showed and what was changed for it.

## Running it

```bash
# The walkers, each alone, with orders (2 hours each for 20M steps)
build/venv/Scripts/python tools/train_animal.py --species deer_red --ranges sandbox --scene animal_orders_train --demo-var orders=true --config training/animal_orders_ppo.yaml --prefix orders --max-steps 20000000
build/venv/Scripts/python tools/train_animal.py --species wolf_2 --ranges sandbox --scene animal_orders_train --demo-var orders=true --config training/animal_orders_ppo.yaml --prefix orders --max-steps 20000000
# A round of the sandbox: four behaviours
build/venv/Scripts/python tools/sandbox_round.py --name sandbox --round 1 --steps 500000
# A round of puppets: the minds only
build/venv/Scripts/python tools/sandbox_round.py --name puppets --round 1 --steps 1000000 --puppet
# What a run's policies do, for 5 minutes, with the animals from above as a video
build/RelWithDebInfo/TrainingEnv.exe scene=sandbox_eval mode=eval var.models=build/runs/sandbox_r1 var.seconds=300 var.out=eval.json var.replay=build/runs/sandbox_r1_replay
build/venv/Scripts/python tools/sandbox_video.py --replay build/runs/sandbox_r1_replay --out build/runs/sandbox_r1.mp4
# In the viewer: the ragdolls as shapes, a run's policies in them (the scripted minds where the run has none)
build/RelWithDebInfo/dynamical.exe scene=sandbox_view var.models=build/runs/sandbox_r1
```

Scenes: `sandbox_train`, `sandbox_puppet` (minds only), `sandbox_heuristic` (puppets and the scripted minds),
`sandbox_eval` and `sandbox_view` (`Policies@act`: the models of `var.models`, `var.prefix=Animal_` for the walkers
alone; `var.heuristic` answers the minds no model answers; `var.puppet`), `animal_orders_train`, `animal_orders_eval`.
`var.life=agents/params/life_hunters.params.json` for the sandbox with men.

**Inference** (`src/onnx/agent_policy.{h,cpp}`): `AgentPolicy` runs an exported model of any layout (several
observations, buffers, continuous and discrete actions) on an `AgentIO`; the world system `Policies` gives every
agent that asked its behaviour's model's action, as the training host gives it the trainer's. The single animal's
`Policy` takes a walker with its order sensor through the same class (`makePolicyHandle`).

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
others. `training/pursuit_ppo.yaml`, `tools/train_sphere.py --scene pursuit [--var runners_still=true --var
approach_reward=0.1]`, scene `pursuit_eval` (models through `Policies`).

Three runs of it (`build/runs/pursuit_m1`, `pursuit_still`, `pursuit_approach`):

| Run | Chasers' return, start to end | |
|---|---|---|
| Both learn, 600k steps | 0.44 to 0.22 catches an episode | runners caught 24% to 15% of the time: the runners learn to keep away first |
| Runners stand still, 600k steps | 0.22 to 0.25 | +1 a catch alone is too sparse for 600k steps |
| Runners stand still, +0.1 a metre closer, 400k steps | -0.07 to 7.2 | the chasers learn: the host's observations, actions and rewards hold end to end |

The last one's model is `tests/data/onnx/pursuit_chaser.onnx`: in the engine (`AP.3`) those chasers catch 44
standing runners in 600 windows of 4 worlds, chasers without a model 1.

## Results (2026-11-11)

**Cost.** A world of 12 deer and 4 wolves (360 bodies, 32 agents) steps 1260 ticks a second on one thread with
zero actions, 21 times real time; with the walkers' models run in the engine, 7 times. With men in it (the solver
at 40 velocity and 8 position steps for two legs): 246 ticks a second. In a round of the sandbox the trainer's four
networks are what limits: 5 environments of 4 worlds make about 1500 walker steps and 200 mind decisions a second
for the deer, 0.4 of real time per world.

**The rules hold** (`SANDBOX.*`): the animals stand on zero actions and a deer's store goes down at its basal rate;
the wolf's head thrown at 5 m/s on a deer's leg takes 8% of its health in Attack mode and nothing in Move; a fall
ends the walker's episode, costs health, and the animal is stood up, stunned; a death ends both agents, leaves a
carcass with its meat, and the agents leave at the next window; puppets graze a cell down in a few seconds, a wolf
feeds on a carcass and a deer does not; two adults in Mate mode conceive, the young is born small a gestation later
and grows in two steps, both parents are paid +1.

**Scripted minds on puppets**, 8 minutes (`SANDBOX.9`): the deer graze and breed (7 births, 1 starved), the wolves
kill 3 and feed. `build/runs/sandbox_videos/puppets_scripted.mp4`.

**The walkers alone** (`orders_<species>_sandbox`, 20M steps each, 2 h 12 min each, both at once):

| | First try, 5.9M (stopped) | Second, 20M |
|---|---|---|
| What changed | head 0.1, mode 0.15, GAIL 0.5 | head 0.3, mode 0.4, GAIL 0.3; Feed paid by how far the muzzle comes down; neck and head may touch the ground |
| Feed: the muzzle, in standing heights of the trunk | deer 1.04 (it stands at 1.08): not lowered | deer 0.12 (14 cm over the ground), wolf 0.09 |
| Head against where it is asked to point | deer 22 degrees, wolf 39 | 9.5 and 9.3 (at 12.6M) |
| Mode rewards of 0.4: attack, mate, rest | (of 0.15) 0.085, 0.04, 0.001 | deer 0.25, 0.15, 0.04; wolf 0.36, 0.18, 0.04 |
| Falls per 33 s episode, sampled actions | half (at 5M, deterministic) | deer 14%, wolf 3% |
| Speed against the command | 0.54, 0.43 | 1.44, 1.62: they overrun it |

The first try walked through every order: GAIL paid 0.38 a step and the command 0.22, against 0.06 for the head and
0.03 for the mode. The second obeys Feed, Attack and the head; it does not stand still (Rest 0.04 of 0.4) and goes
faster than asked, the command's reward having no ceiling. With orders the reward now takes off what the animal goes
over the speed asked (`rateAnimal`); the walkers learn that in the sandbox (below). Deterministic actions are not
these walkers' own: the wolf falls in 61% of its episodes on the mean action and 3% sampled. `Policies` runs them
sampled (`var.deterministic=false`).

**What the walkers charge.** Standing costs nothing over the basal rate (0.0006 of the reference power). Walking at
the speeds the minds ask costs 3 to 6.7 times the reference (0.1 m g v): the jolts of the drives more than the work
of a gait. `work` went from 1 to 0.4 to 0.1; at 0.1 walking takes a store about half as fast again, as it does for a
puppet.

**The ragdolls with scripted minds** (20M walkers, 6 minutes, `walkers20m_scripted_*.mp4`): the deer graze with their
own necks (the grass goes down), wolves land 46 blows with their heads and kill one deer. Everyone starved in the
end: that run still charged work at 0.4.

**Rounds of puppets** (minds only; what each showed is in the notes of the next reward file; rounds 5 to 7 on the
dense arena, `life_dense.params.json`: 120 m, 8 wolves):

| Round | Deer | Wolves |
|---|---|---|
| 1: eat, hurt, fear, court; wound, kill, hunt (500k decisions) | all starve; feeding 2% to 7% of the time; return -1 | all starve; no kill; return 0.2 to 0.9 |
| 2: eating pays 4 times more, the posture of grazing and the place pay, hunger costs (1M) | the deer graze: feeding 9% to 57% of the time, store 0.48 to 0.91, life in the world 225 s to 640 s, two thirds die of old age; no conception (Mate mode 17% to 4%); return 0.7 to 22 | nothing learned: all starve, modes at a fifth each, entropy 2.99 to 2.93 |
| 3: courting from 100 m, the herd, pairing; closing in from 150 m, strike and gnaw pay more; deer brought in under 8 (1.5M) | surviving is learned: feeding 74% of the time, store 0.96, four in five die of old age after 800 s; still no conception (Mate mode 2%): the court term was collected while grazing | return 7 to 11, none of it hunting: paid for being within 150 m of a deer; all starve |
| 4: eating pays only under 0.85 of a store; courting, chasing and fleeing pay by the metre closed (`mate_closing`, `prey_closing`, `predator_closing`) (1.5M) | feeding 73% to 28% of the time with the store still at 0.92; Mate mode 2% to 16%; the court term starts to pay; no pair side by side, no conception | nothing: chase, strike and gnaw pay zero, all starve |
| 5: courting and chasing paid twice as much; the buffer marks possible mates; the dense arena (1.7M and 1M) | half of their deaths are now kills, half old age, hunger 1%; a life lasts 630 s; pairs begin to form (near and pair pay), no conception | **the wolves hunt**: Attack mode 23% to 41%, speed 0.42 to 0.72 walk speeds, wounds dealt six times up, kills 1.2 to 2.1 per 20k decisions; return 3 to 26. They do not eat what they kill: 96% starve |
| 6: the meal paid more (Feed mode on a carcass, closing on it, eating); fear and pairing paid twice as much (2.2M and 1.3M) | **the deer learn the wolves**: four in five deaths were kills at the start of the round, none at its end; 98% die of old age after 790 s; no conception | kills 6.4 per 20k decisions at the start, 1.4 at the end as the deer keep away; Feed mode 23% to 31%, feeding 1% to 2% of the time; 96% starve; return 29 to 54 |
| 7: a pair needs 2 s within two lengths (not 3 s within one and a half) and is paid as its time together builds (`mating`); eating pays the wolves by what is eaten (2.2M and 1.3M) | **the first conceptions and births by learned minds** (two of each, early), then none: the return went from 4 to 99 on the together term alone. Hovering just short of a conception paid more than conceiving, which switches the term off. 99% die of old age | live longer (243 s to 351 s; one in twenty reaches old age, 86% starve), feed 2% of the time, kill 1.5 deer per 20k decisions |
| 8: together pays twenty times less; conceiving and carrying pay more than a pair that waits (2.3M and 1.3M) | the hovering stopped (together and pair pay 0.001 to 0.002) and births go on through the round, a few: one life in fifty leaves a young; 88% die of old age, a life lasts 730 s; return -3 to 35 | 85% starve, 3% reach old age; feeding 0.5% to 2.7% of the time; kills 1.1 to 1.8 per 20k decisions; no conception |

What the rounds taught about writing the terms: a term has to be worth something next to a death of -1 (round 1);
a term that pays for where the animal is gets collected where it stands (the court of round 3, the hunt of round
3): what moves an animal is paid by the metre (rounds 4 and 5); and the animals have to meet for any of it to
happen (the dense arena of round 5). Round 1 also showed the trainer needed a smaller buffer: with 20480 it updated
the minds 24 times in 500k decisions.

**Rounds of the sandbox itself, first line** (`sandbox_r1` to `r3`; four behaviours: walkers from their 20M runs,
minds from `puppets_r5`, the dense arena, `walkerShare` 0.1; `sandbox_r1_*.mp4`, `sandbox_r2_*.mp4`). Superseded: Paul
asked on 2026-11-11 for the minds to start from scratch on the pretrained walkers (next paragraph).

| Round | Deer | Wolves | Walkers |
|---|---|---|---|
| 1 (420k decisions of the deer's minds, 2.6M steps of their walkers, 35 min) | graze with their own necks: feeding 18% of the time, the grass goes down; a life in the world 100 s to 320 s; all still starve; return -2.6 to 2.0 | wound and kill with their heads: 2.4 healths of wounds and one kill per summary; a life 66 s to 190 s; 92% starve; return 4 to 27 | learn the speed ceiling: 1.0 to 0.86 (deer) and 1.85 to 1.1 (wolf) of the speed asked; episodes ended by a fall 43% to 3% and 45% to 8%; head 13 degrees off |
| 2 (from round 1, round 7's reward files; 720k decisions, 4.4M walker steps, 62 min) | **the ragdoll deer live**: 99% die of old age (all starved at the start of the round), a life in the world 130 s to 650 s, feeding 20% of the time, store 0.73; return -1.7 to 17 | begin to eat what they kill with their own muzzles (feeding 0.1% to 2.9% of the time, Feed mode 23% to 39%); a life 150 s to 290 s; all still starve; one or two kills per summary; return 8 to 50 | episodes ended by a fall 29% to 1.4% (deer) and 47% to 0.9% (wolf); 0.93 and 0.99 of the speed asked; returns 453 to 604 and 436 to 523 |
| 3 (from round 2, round 8's reward files; stopped at 480k of 700k decisions when the line below replaced it) | feeding 18% to 20% of the time | feeding 0% to 1.5% | returns 469 to 632 and 510 to 584 |

In the engine (`sandbox_eval`, the round's four models, sampled): round 1, 6 minutes: 22 blows land, nobody is
killed, a deer lasts 230 s. Round 2, 10 minutes: 11 of the 12 deer are alive at the end (one starved; one fall
between them in ten minutes), no deer is killed (10 blows), 12 wolves starve. `sandbox_r2_*.mp4`.

**The line of record** (`scratch_r1` onwards; Paul, 2026-11-11: "pretrain the physics controllers for both animals
for 20M then continue from there for action agent from scratch"): the walkers start from their 20M runs
(`orders_deer_red_sandbox`, `orders_wolf_2_sandbox`) and keep learning, the minds start from nothing: no puppet round
behind them. `tools/sandbox_round.py --name scratch --round 1 --steps 1500000 --life agents/params/life_dense.params.json`,
round 8's reward files. Those walkers were trained before the speed ceiling was added to their reward: they learn it
in the sandbox (one round sufficed in the first line).

**Walkers to replace them** (Paul, 2026-11-11: "pretrain the controllers for 80M for future replacement"):
`orders80_deer_red_sandbox` and `orders80_wolf_2_sandbox`, the same training as the 20M ones with the reward as it
now is (the speed ceiling from the start), 80M steps each, started 2026-11-11 22:53, about 8 hours at 2700 steps a
second each. A round takes them with `tools/sandbox_round.py --walkers orders80` (round 1 of a new name: a round
that continues another keeps that one's walkers).

| Round | Deer | Wolves | Walkers |
|---|---|---|---|
| 1 (minds from nothing, round 8's reward files; 1.58M decisions of the deer's minds, 9.6M steps of their walkers, 2 h 40 min next to the two 80M trainings) | **learn to live from nothing**: 65% of those that die in the last quarter of the round die of old age (the first quarter says nothing: no life is long enough yet to end that way); a life in the world 143 s to 609 s; feeding 8% to 16% of the time, store 0.46 to 0.70; they hardly move (0.17 of a walk speed), no pair forms, no conception; return -1.9 to 10.8 | no hunt: all starve after 260 s, modes at a fifth each, wounds dealt 0.42 to 0.09 a summary, nothing eaten; paid for where they stand (`close` 0.0022 a decision against 0.0005 for the chase); return 0.7 to 4.1 | episodes ended by a fall 14% to 2.7% (deer) and 33% to 0.5% (wolf); 0.92 and 0.96 of the speed asked (1.04 and 1.20 at the start: the ceiling is learned); head 11 and 8 degrees off; returns 210 to 442 and 205 to 422 |
| 2 (from round 1, round 9's reward files: the wolves' `close` only within 6 m, their chase by the metre 2.5 times more, wound, kill and meal more; the deer's courting by the metre twice as much; 2.14M decisions, 12.9M walker steps, 3 h) | a little worse: 38% die of old age, 61% starve, 1% are killed; a life in the world 445 s; feeding 15% of the time, store 0.65; no pair side by side, no conception; return 6.0 | **the hunt begins from nothing**: speed 0.25 to 0.80 of a walk speed, Attack mode 24% to 37%, wounds dealt 0.57 to 1.18 healths a summary, a kill in one summary of thirteen; they walk to carcasses and do not eat (feeding 0.09% of the time, Feed mode 18% to 14%); 98% starve after 240 s; return 4.7 to 7.6 | episodes ended by a fall 1.6% for both; 0.98 and 1.01 of the speed asked; head 9.7 and 7.8 degrees off; returns 446 to 514 and 426 to 550 |
| 3 (from round 2, round 10's reward files: the wolves' `gnaw` pays by how far the muzzle comes down by a carcass; the deer's courting, nearness and pairing only over a store of 0.6, hunger twice as costly; 2M decisions, started 2026-11-12 03:42) | SCRATCH3_DEER | SCRATCH3_WOLF | SCRATCH3_WALKERS |

In the engine (`sandbox_eval`, round 1's four models, sampled, 10 minutes): 10 deer alive at the end, 6 starved (of
16 with those brought in), 2 falls between them; 13 wolves starve, 4 blows, no kill. `scratch_r1_*.mp4`. Round 2: 44 blows, no kill, 14 wolves starve; 14 deer starve, each about 130 s after it came in, while the herd keeps its ten. `scratch_r2_*.mp4`.

**Why deer still starve.** A store of 0.76 lasts 130 s: the deer that die never ate. Animals are brought in on the border of the arena; with grass on 55% of it in patches of 60 m, many land on bare ground further from grass than the 10 m of the mind's grass rings, and no term paid for going to it (`pasture` pays once within 3 m). Two signals were added for round 4, `grass_distance` (m to the nearest grass over 0.1 m within 40 m) and `grass_closing` (m closed on it over the decision, as the other closings), `GrassField::nearest`, `LIFE.3`.

Against the first line: minds that had three million decisions as puppets behind them wounded and killed from the
first round on ragdolls; from nothing the wolves' minds find no hunt in a million decisions, while grazing, which
needs no other animal, is found in one round either way.

**The man's walker with orders** (`orders_homo_heidelbergensis_walking_sandbox`, 20M steps, the solver at 40 and 8
steps): it walks, which the library's men did not at 2M without orders: 0.95 m/s, 0.81 of the speed asked, the head
within 7.5 degrees of where it is asked, standing in Mate mode paid 0.27 of 0.4. It falls in 42% of its 33 s
episodes (sampled) and does not bring its head down to feed (a man does not graze: feeding on a carcass needs the
muzzle within reach of it, not on the ground). A first round with men (`hunters_r1`, six behaviours) started and
ran: stopped after 4 minutes with the first line.

## Milestones

| | What | State |
|---|---|---|
| M1 | Several agents and behaviours in a world through the training host; `pursuit` | Built. `MA.*`, `PURSUIT.*`; chasers learn with a dense reward and catch through `Policies` |
| M2 | `AnimalBot` as a component, ragdolls that collide with each other, a full world | Built. `SANDBOX.1`, `.2`; 21 times real time |
| M3 | Walker with orders; the deer's and the wolf's walkers alone, from standing to 2.6 walk speeds | Built and trained (20M each). They lower the head to feed, throw it to attack, point it within 10 degrees; they do not stand still and learned the speed ceiling only in the sandbox |
| M4 | Rules of life; scripted minds | Built. `LIFE.*`, `SANDBOX.3` to `.10` |
| M5 | Minds: senses, reward files and their evaluator, `sandbox_train`, `tools/sandbox_round.py` | Built. `EXPR.*`, `SANDBOX.8`, `MA.5` |
| M6 | Rounds: reward files rewritten from each round's summary | Eight rounds on puppets, two and a half of the sandbox with minds from puppets, then the line of record with minds from scratch. Learned on puppets: grazing, hunting, fleeing, and a few births (one life in fifty). Learned by the ragdolls: grazing to old age, wounding, the first meals on a kill. Not learned: wolves that live off what they kill |
| M7 | Inference: `Policies`, `sandbox_eval`, `sandbox_view`, videos from above and in the viewer | Built. `AP.*`; a round's four models run in the engine. Not in `forest_herds` (its herds are skinned meshes on a terrain: the sandbox is flat and drawn as shapes) |
| M8 | Men as hunters: a third species with a spear in the forearm | Built (`life_hunters.params.json`, `SANDBOX.10`, scripted on puppets). Its walker with orders is trained (20M: it walks, falls in 42% of its episodes). A round of six behaviours starts and runs; none run to its end |
| later | The terrain; trees and what they hide; the other herd kinds; `lifeScale` raised as lives get longer; the skinned meshes on the ragdolls | |

## Risks, as they stand

- **Births.** Rare: on puppets one deer's life in fifty leaves a young after eight rounds, the wolves none; the
  ragdolls none. A reward that pays the way to a conception is collected for itself the moment it is worth more
  than the conception (round 7): every such term has to be checked against what it leads to.
- **Wolves do not eat.** They make carcasses and starve next to them. Feeding needs Feed mode, the trunk slow and the
  muzzle within reach of a body of the carcass; the puppets' wolves do it 2% of the time.
- **The ragdolls starve.** In the sandbox itself a deer lasts 320 s and a wolf 190 s after one round: they graze and
  wound, not enough. The rounds are short for what they have to teach: 0.4 of real time per world, a life of
  minutes, 35 minutes a round.
- **The walkers do not stand still**, and moving costs them 3 to 7 times what a gait should. Both are theirs to
  learn in the sandbox now (the speed ceiling was learned in one round); a walker trained alone with the ceiling
  from the start would be the cleaner fix (two hours each).
- **Four networks under each other.** No sign of trouble in two rounds: the walkers' returns went up while the minds
  changed. Too short to say more.
- **Deterministic actions.** The walkers fall on their mean action (the wolf in 61% of its episodes) and not when
  sampled: whatever plays these policies has to sample.
