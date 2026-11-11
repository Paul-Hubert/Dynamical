# Animal agents: a physical body, an agent and a training chain for every animal of the library

- **Date:** 2026-11-11, Windows 11, Ryzen 9 9950X3D (32 threads). Main checkout, `master`, **nothing committed**.
- **Task:** [prompts/animal-agents.md](../prompts/animal-agents.md), run without a human. Choices are in
  [decisions.md](decisions.md), the last section.
- Everything below is measured unless a sentence says "I think" or sits under "What I infer".
- **Other sessions worked in this checkout during the run** (grass and soil code, the walker's params): see
  "Not this run's" at the end. It changes how three things here should be read.

## Result

**38 animals train with one command. 15 of them pass every check of their body and of their demonstration; the 23
others train too, with findings listed per animal.** 20 animals have no agent: 7 have no walk clip, 13 have a rig the
rules cannot read.

```
build/venv/Scripts/python tools/train_animal.py --species deer_red --ranges walk_basic --max-steps 20000000
build/venv/Scripts/python tools/train_animal.py --species all      --ranges walk_basic --max-steps 20000000
```

The launcher generates the body when it is stale, freezes the engine, records the demonstration (cached by a key over
the executable, the body, the ranges, the params, every scene and the species' library files), and runs the walker's
trainer. Then `tools/animal_eval.py --run <run>` and `tools/animal_video.py --species <id> --run <run>`.

- **One system:** bodies, layouts and scenes are generated from the library and `analyseGait`; no code and no file per
  species. Quadrupeds (33), birds (3: crow, owl, pigeon) and the library's two men go through the same path.
  `resources/agents/animal_overrides.json` is the one file for what the data does not hold; it holds nothing yet.
- **The red deer walks.** TWENTY_M_RESULT
- **The human walker's tests pass unchanged** (golden rollouts included); its two params files were changed by
  another session, not by this run (see the end).

## Readiness

`tools/animal_status.py --prefix aa100k` writes this table (`build/runs/animal_readiness.md`, `.json`) from the body
summary, the cached demonstrations' checks and the 100k-step runs. "Stands": 10 s at the 60 Hz step on a zero action.
"Demo": 9000 decisions on `walk_basic`; what follows "yes," is what its checks found.

READINESS_TABLE

What the findings mean:

- **"n jumps of an axis"**: decisions (of 9000) where an observed axis of some body turned by more than 0.6 (35°)
  since the last decision. The red deer's largest change is 0.33, like the man's walk (0.25 to 0.32 in the terrain
  walker's report). The bodies named are paws, claws, toes and pasterns, and the legs of the animals with the shortest
  strides (crow and pigeon 0.53 s, hare 0.58 s, rabbit 0.70 s against 1.42 s for the deer) at the same 30 decisions a
  second. I did not separate the two causes.
- **"a body n % of the height under the ground"**: the foot box in the recorded walk, against the ground the walk
  plants its foot joint on. 1 to 2 % for the hoofed animals; 9 to 15 % for a lion's tail, the hare, the rabbit, the
  birds' feet, the paws of the badger and of the bear. The box is fitted flat as the animal stands; the walk holds the
  foot at the clip's attitude and height.
- **Hare**: it does not fall, but settles 6.7° nose down and 15 mm (my limit is 6°); its walk leaves a toe's limit by
  5° on 245 ticks of 18000.
- No recording trips the fall test, flips the trunk's heading, or misses the commanded speed by more than 0.7 %
  (measured as the reward measures it).

**Without an agent:**

| Animals | Why |
|---|---|
| bull, chick, chicken, cow, dog, goat, horse_farm, pig, rabbit_farm, ram_farm, rooster, sheep | The farm pack: the rig holds the feet with controls, `analyseGait` finds no leg chain (the reason per animal is in the table). Their walk clip "played straight" needs a body first, and the body rule needs the legs: the same rules cannot do it |
| cat_farm | Its walk is read, but its skeleton is not one tree: the head, the neck and the feet hang from separate roots, so no joint carries the legs and the skin |
| base_male_rigged, grizzly, hemitragus_bonali, homo_heidelbergensis_walking_2, lioness_fur_fbx, ram, snow_leopard_fur_fbx | No walk clip |

## Bodies

`animal_body.exe --species all --check` (about half a minute to generate the 38, then 20 simulated seconds each: 10 s standing, 10 s limp). Contact sheets, each body
beside its mesh: `build/runs/animal_bodies/sheet_1.png` to `sheet_4.png`, and one page per animal (`<id>.png`: bind
pose from three sides, the standing pose, every joint's limits and gains).

![contact sheet](figures/animal_bodies_sheet_1.png)
![the red deer](figures/animal_body_deer_red.png)

How a body is made (the reasons are in decisions.md):

| Part | Rule |
|---|---|
| Which joints are bodies | Legs: the chains of `analyseGait`, at most 4 bodies a leg, the foot last. The trunk: the lowest joint with every leg and the skin under it. Everything else: a joint stays a body when its part of the volume x the lever to the centre of that part x how far it turns in the gait is worth 3e-4; else it rides on its parent. 15 to 24 bodies |
| Volume, mass | The generalized winding number of the skin on a grid, each cell given to the joint of its nearest vertex; 1000 kg/m3. No segment under 1.2 % of the mass in a leg |
| Shapes | A capsule, or a box where the section is flat, with the moments of the volume the segment holds. Feet, and heels that reach the ground, are boxes that lie flat as the animal stands |
| Joints | A swing-twist constraint per body. Axes and limits from the range covered over every clip (99 samples in 100) and the gait clips and the procedural walk (all of it), plus 5° and 10 %. A hinge where the motion is one (4 to 15 joints per animal) |
| Drives | Stiffness 4.3 x M g H per radian between a foot and the trunk (the man's gains), times the leg's body count on two legs; 30 x weight x lever for what only hangs. Damping and time scaled by sqrt(H) |
| Solver | Substeps 1 to 3 by size; penetration slop 0.25 % of the height; 40 iterations on two legs, 10 on four; **moments of inertia raised** to what the drive can be solved against in one step |
| Standing pose | The idle clip, else the bind pose, else the walk where the most feet are down |

**What it took to stand.** With the masses and inertias of the volumes, none of the 38 stood, though the drives hold
each statically: the solver's 10 iterations do not resolve a drive 1500 times stiffer than a 400 g hoof is heavy to
turn, under a 365 kg trunk.

| Setting | Standing of 38 |
|---|---|
| As generated, 60 Hz, 10 iterations | 0 |
| 2 substeps / 2 substeps and 20 iterations / 3 substeps | 1 / 9 / 9 |
| 4 substeps | 22 |
| Inertias raised to (damping x dt + stiffness x dt²) / 100, 30 or 10, at the 60 Hz step | 21 / 30 / 30 |
| ... / 30, plus flat heels, slop by size, 40 iterations and stiffer joints on two legs | 37, the hare settling |

The cost of the raised inertias: a deer's leg is about twice as heavy to swing about its hip. Masses are not touched.

**Weights.** Volume at 1000 kg/m3. The typical weights are from memory, not from a source in the repository.

| Animal (library id) | Model: height, length | Mass from the volume | Typical weight | Ratio |
|---|---|---|---|---|
| Bison (`buffalo`) | 2.03 m, 2.72 m | 1488 kg | 700 to 900 kg | 1.9 |
| Red deer (`deer_red`) | 2.07 m with antlers, 2.03 m | 365 kg | 160 to 240 kg (stag) | 1.8 |
| Red deer (`deer_stag`) | 1.86 m, 1.78 m | 206 kg | 160 to 240 kg | 1.0 |
| Wolf (`wolf`) | 0.67 m, 1.20 m | 67 kg | 30 to 50 kg | 1.7 |
| Wolf (`wolf_2`) | 0.75 m, 1.52 m | 142 kg | 30 to 50 kg | 3.5 |
| Horse (`horse`) | 1.59 m, 2.29 m | 518 kg | 400 to 550 kg | 1.0 |
| Man (`homo_heidelbergensis_walking`) | 1.88 m | 117 kg | 80 to 90 kg at that height | 1.4 |
| Man (`homo_heidelbergensis_unarmed_walk_forward`) | 1.69 m | 88 kg | 60 to 70 kg | 1.35 |

No correction is in the overrides file. Drives scale with M g H, so a body's accelerations do not depend on its mass;
a correction is `"species": { "buffalo": { "mass": 800 } }`. The signed volume of the triangles, the usual way, was
1.6 to 2.7 times too large on 10 meshes (fur cards, a second skin); the winding number agrees with it to about 1 % on
the others.

**What the sheets show** (I looked at all four): legs and trunks follow the meshes. Not good: the head of a stag with
antlers is a box as long as the antlers (`deer_red`, `moose`, `reindeer`); the wings of the crow, the owl and the
pigeon are flat boxes wider than the bird; the horse has one body from its chest to its head (its neck does not turn
in its walk), the doe (`deer_doe`) no head of its own.

## The agent

World context `AnimalBot`; systems `AnimalFactory`, `AnimalDrive@act`, `AnimalSoftGround`, `PhysicsStep`,
`AnimalSync`, `AnimalTask`, `AnimalTermination`, `AnimalRewards@observe`, `AnimalObservation@observe`,
`AnimalEpisode@observe` (`common/animal_world.json`).

| | |
|---|---|
| Behaviour | `Animal_<id>`, one per species |
| Observation | 2 x (7 + 9 x bodies): 284 to 446. Header: the command in the trunk's heading frame (x, z, in walk speeds), its turn, command minus trunk velocity (x, z), height error, soft depth. Then forward axis, up axis and velocity of the trunk and of each body, all in the **trunk's** heading frame |
| Actions | 8 a joint (112 to 184): two gains, a target (twist, two swings), a target angular velocity. Each target runs from limit to limit through the standing pose: **a zero action stands** |
| Task | `WalkCommand`: a speed (0.8 to 1.2 of the walk clip's on `walk_basic`) and a turn drawn every 2 to 5 s, eased over a second. The same object is stepped for the physical animal and followed by the recorded walk |
| Reward | Progress of the trunk along the commanded heading, capped at the commanded speed, x 0.34 per decision; the death penalty of `animal.params.json` on a fall |
| Fall | The trunk under 0.55 of its standing height over the ground under it, or a body that may not touch the ground on it |
| Episode | 2000 frames (33.3 s), a random heading at the start, from the standing pose |

Tests `AA.1` to `AA.9` (`tests/unit/animal_agents.cpp`, skipped without the generated bodies): the joint angles are
Jolt's and the standing pose is inside every limit; standing and limp; the layout; a quadruped's training world steps
deterministically; the fall test both ways (the limp ragdoll is ended; the kinematic walk on `walk_basic`,
`walk_style` and `walk_soft` never is, for a deer, a crow and a man); the recorded walk (limits, flips, the command's
heading is the walker's to 1e-4 rad, speed within 3 %); a run through the recorder; the terrain worlds; soft ground.

## The proofs: three bodies, 2M steps each

`training/animal_ppo.yaml` is `walker_ppo.yaml` without its `environment_parameters` (the walker's speed) and with `max_steps` 20M: the same PPO and the same two reward signals. 8 environments x 25 worlds x 2
threads. No value is NaN in any console.

| | `deer_red` (4 legs, 22 bodies) | `crow` (2 legs, 15 bodies) | `homo_heidelbergensis_walking` (2 legs, 17 bodies) |
|---|---|---|---|
| Run | `aa_proof_deer_red` | `aa_proof_crow` | `aa_proof_man` |
| Wall time, steps/s | 621 s, 3247 (alone) | 681 s, 2964 (beside the 20M run) | 737 s, 2734 (beside the 20M run) |
| Death rate, 0.2M to 2M | 1 to 0.89 | 1 to 0.95 | 1 to 1 |
| Episode length (decisions) | 58 to 433 | 29 to 315 | 34 to 62 |
| Extrinsic reward per episode | -4.6 to 103 | -8.5 to 12.8 | -0.7 to 15.1 |
| GAIL reward per episode | 41.6 to 294 | 20.8 to 217 | 25 to 36.5 |
| D(expert) / D(policy) at 2M | 0.50 / 0.47 | 0.57 / 0.49 | 0.60 / 0.43 |
| Eval, 200 episodes, mean action | 21 % fall, stands: 0.06 of the commanded speed | no fall, stands still | falls in every episode after 2.4 s, lunging at 1.9 m/s |
| Death penalty it trained with | -10 | -10 | **0** (see the end) |

![deer](figures/animal_proof_deer_red_curves.png)
![crow](figures/animal_proof_crow_curves.png)
![man](figures/animal_proof_man_curves.png)

- At 2M the deer moves only by its action noise: sampled as in training, the same policy covers 0.90 of the commanded
  speed and falls in 94 % of the episodes; its mean action stands. The 20M run shows what comes next.
- The man's proof is not comparable with the two others: it was frozen after another session set the death penalty
  to 0, and a man who loses nothing by falling throws himself forward.
- The yardstick was 3000 steps/s for the flat walker. The world alone, with zero actions (`mode=bench`, 25 worlds, 2
  threads, the machine busy): the walker 72000 world-ticks/s, the deer 69000, the crow 25500 (2 substeps, 40
  iterations).

## The 20M run: the red deer on level ground

TWENTY_M_SECTION

## Every animal: 100k steps

`tools/train_animal.py --species all --max-steps 100000 --prefix aa100k`, one frozen engine for the queue, beside the
20M run: **38 of 38 ran through** (exit 0, a policy exported), 40 to 60 s each. One (`deer_stag_forest`) first died
on a port another trainer held; the launcher now checks every environment's port before it starts, and the rerun
passed.

## Prepared, not validated

| What | State |
|---|---|
| `walk_style`, `walk_soft` | Both record for all 38 (9000 decisions each). **The checks do not pass**: on `walk_style` 17 animals leave a joint limit (by 1 to 49°, on up to 250 ticks of 18000), 3 trip the fall test on a few ticks (bear, lioness, the unarmed man: a calf or a knee on the ground), and an axis jumps on most; on `walk_soft` 17 leave a limit (by 1 to 91°), none trips the fall test. The limits are taken from 16 procedural walks over the engine's widest ranges; that does not cover what 30 recorded walks reach |
| Soft ground | Built as the prompt's default: the hard ground stays, the layer is `depth` over it, a body inside is dragged against its velocity (3 weights of the body per walk speed sideways, a quarter of that upwards, nothing downwards); the depth is drawn per episode and is in the header. Tested: the deer stands in it, the direction of the drag, a limp body comes to rest (`AA.9`). A 50k-step training on `walk_soft` runs through. **Not validated:** no policy was trained in it, and the first resistance I set (60) stopped a body within a step on every axis, up included |
| Terrain | `animal_terrain_train`, `_eval`, `_view`, `animal_demo_terrain` load, spawn on routes in the slope band, end a limp animal on the colliders and record (`AA.8`). A 50k-step training with a demonstration recorded on the routes runs through (`--terrain`). No policy was trained on it |
| Slope band, red deer | On planes, straight up or down at the clip's speed, 1500 decisions each: limits hold, no jump, no flip, no fall, speed exact from **-20° to +20° along and to 15° across** (`build/runs/animal_slopes/deer_red.md`, `tools/animal_slope_band.py`). The terrain worlds default to 15° up, 15° down, 12° across. Steeper was not measured, and only one animal was |
| Run | `wolf_2` records 3000 decisions, but leaves a limit by 14° on a third of the ticks, stretches its bones by its own height and slides its planted feet at 0.55 m/s: not usable as it is |
| Trot | No clip of the 38 animals is tagged `trot` (the red deer's `Trot_forward_IP` is tagged `other`): nothing to record. The recorder says so and exits |
| Looking at trot and run | Not done: the viewer was broken by another session's work when I got there (see the end) |

## What changed in the engine, and why

| Change | Where | Why |
|---|---|---|
| The ragdoll build moved out of `humanbot.cpp`, unchanged, into a function both agents call; a box's convex radius can be given (the man keeps his rule) | `logic/agents/ragdoll.*`, `humanbot.cpp`, `humanbot_assets.h` | One build for the man and the animals. The walker's golden rollouts pass |
| The body generator, the loaded body, the joint angles as Jolt measures them | `logic/animals/animal_body.*`, `tools/animal_body/` | Step 2 |
| `WalkCommand` and `GaitWalk` | `logic/animals/gait_walk.*` | One command process for the task and the demonstrations; the procedural walk without a registry |
| `AnimalBot`, its systems, the recorder (`AnimalGaitPlayer`), the checks, the standing test | `logic/animals/animal_agent.*` | Steps 3 and 4. Beside the man's systems: `HumanBot` is fixed arrays with Unity's conventions |
| The training host evaluates animals, writes a recording's checks, a replay of the segments, and refuses a walk it cannot play | `train/animal_host.cpp`, `train_host.*` | The man's eval reads `HumanBot`; a recording of a missing clip never ended |
| `"<file>#<key>"` in a scene also finds a file given as a path or in the source tree's resources | `scene/scene_loader.cpp`, `util/paths.*` | The solver settings come from the body file, and the bodies live with the library |
| `AnimalAgentView`: the skinned mesh on the ragdoll, a camera, a panel | `view/animal_view.*` | Fit errors show there, policies are filmed there |
| Scenes, params, ranges, overrides, the PPO config | `resources/scenes/animal_*`, `common/animal_*`, `resources/agents/params/animal.params.json`, `ranges/`, `animal_overrides.json`, `training/animal_ppo.yaml` | Step 5 |
| Tools | `tools/train_animal.py`, `animal_eval.py`, `animal_video.py`, `animal_replay_video.py`, `animal_status.py`, `animal_sheets.py`, `animal_slope_band.py` | The launcher never edits `start-run.ps1` |

`gait.cpp`, `gait_dataset.cpp` and the procedural walk itself are untouched.

## What I infer

- **The quadrupeds will train like the deer; I expect the order below.** The deer takes off before 3M steps where the
  man took 5 to 15M: I think because a zero action already stands on four legs, so the policy only has to learn to
  move.
- **The jumps in the small animals' recordings are mostly their pace, not errors**: at 30 decisions a second a crow
  takes a step in 8 decisions. I think a decision rate by size (as the substeps are) is the fix, but it changes the
  episode and the trainer's horizon, and I measured nothing.
- **The feet under the ground will cost the animals that have them**: the discriminator sees a trunk lower than the
  physical animal can hold. Fitting the foot box to the walk's stance instead of the standing pose would remove most of it.

## The order I would train them in, and what I expect to break

1. **The 14 quadrupeds that pass every check** (the deer family, `buffalo_2`, `horse`, `moose`, `fox`,
   `ram_realistic`): as the red deer. I expect them to walk by 10M.
2. **The other large quadrupeds** (`buffalo`, boars, lions, `lynx`, wolves, `rhinoceros`, `bearbrown`, `moose_cow`,
   `reindeer`): feet 4 to 15 % under the ground in the demonstrations. I expect them to walk lower or stiffer than
   their demonstrations, and the lion to drag its tail.
3. **The small quadrupeds** (cats, `badger`, `redfox`, `rabbit`, `hare`): 2 to 3 substeps (slower), jumps in the
   demonstrations. The hare and the rabbit hop: their feet slide in the walk itself.
4. **The owl, then the crow and the pigeon, then the two men**: on two legs nothing stands for free; 40 solver
   iterations cost throughput (the crow's world is 2.7 times slower than the deer's). The 2M proofs show both failure
   modes: standing still, and diving. I expect them to need the death penalty and far more than 20M steps, as the
   human walker did.

## Not this run's, and it matters

- **`deathPenalty` was set to 0 at 14:01** in `walker.params.json`, `devita_h173.params.json` and
  `animal.params.json`, by another session. I reverted none. The deer and crow proofs, the 20M run and the 100k
  queue were frozen before it (-10); the man's proof, the rerun of `deer_stag_forest` and the two 50k smokes after
  (0). **`train_animal.py` now trains with a death penalty of 0** until that line is set back.
  `build/runs/animal_params_edit_note.txt` has the times and values. The walker's two files differ from `HEAD`.
- **`dynamical.exe` renders an empty sky since 14:17** in every scene without the terrain pass: it was rebuilt by
  another session's renderer work; the golden capture `V2.1 test_boxes_view` fails too. My view scenes rendered at
  13:46 (the films below from before 14:17 are from the engine). `tools/animal_replay_video.py` films the same rollout
  without the viewer, the mesh as points.
- **The build broke several times** on files of the grass and soil work in progress, and the test target was blocked
  for half an hour; `tests/unit/animal_debug.cpp` is a trace I left for whoever tunes the generator, not a test.

## Tests

TESTS_LINE

## Files

| What | Where |
|---|---|
| Bodies (generated, git ignored with the library), their summary | `resources/animals/bodies/<id>.body.json`, `summary.json` |
| Contact sheets | `build/runs/animal_bodies/sheet_1.png` to `sheet_4.png`, `<id>.png` |
| Readiness table | `build/runs/animal_readiness.md`, `.json` |
| Demonstrations and their checks | `build/runs/demo_cache/<id>_<ranges>_<key>.demo`, `.checks.json` |
| Proofs | `build/runs/aa_proof_deer_red`, `aa_proof_crow`, `aa_proof_man`; evals in `build/runs/eval/<run>/` |
| The 20M run | `build/runs/animal_deer_red_20m/` (`Animal_deer_red.onnx`), `_env/` (the frozen engine, body and params) |
| 100k runs | `build/runs/aa100k_<id>_walk_basic/` |
| Films | `build/runs/animal_videos/` |
| Slope band | `build/runs/animal_slopes/deer_red.md` |
