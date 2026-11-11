# Autonomous run: physical animals, ready for demonstrations and training

Paste everything below the line into a Claude Code session started in the main checkout
(`C:\Users\Paul\Desktop\Projects\Dynamical`, branch `master`: the animal library only exists there), or
run it headless:

```
claude -p "$(cat docs/physical-agents/prompts/animal-agents.md)" --permission-mode acceptEdits
```

---

You are building the deep reinforcement learning system for the animals of this repository, **fully
autonomously**. Nobody answers questions during this run.

Today the animals are skinned meshes that play clips and a procedural walk (`docs/animals.md`). The
human walker (`docs/physical-agents/`) is a ragdoll on Jolt, driven at its joints by a policy trained
with PPO and GAIL from demonstrations of a kinematic walk. This run gives every animal what the human
walker has: a physical body, an agent (observations, actions, rewards, episodes), demonstrations
recorded from its procedural walk, and the scenes, config and launcher that train it.

**The long trainings are not part of this run.** It ends when each animal can be trained with one
command, and short trainings on a few animals have shown that the whole chain works.

Four things are fixed by Paul:

1. **One system for every animal.** Bodies, layouts and scenes are generated from the library
   (`resources/animals/`) and from the gait analysis (`analyseGait`), with no code and no hand-made
   file per species. Quadrupeds, birds and the library's men go through the same path. One committed
   file of per-species overrides is fine for what the data does not hold (a mass).
2. **Demonstrations come from the procedural walk** (`proceduralWalk`), its style, speed and turns
   drawn from ranges (`GaitRanges`): on level ground, on the terrain, on soft ground. Not from the
   clips played as they are.
3. **Training is the walker's:** the fork's `mlagents-learn` in `build/venv` driving
   `TrainingEnv.exe`, PPO with an extrinsic reward (go where and as fast as commanded, stay up) and
   GAIL on the demonstrations. No new trainer, no new algorithm.
4. **The human walker does not move.** Its golden rollouts (`resources/agents/golden`) pass, its
   scenes load, its trained policies behave as before.

## What exists

Read these first: `CLAUDE.md`, `docs/animals.md`, `docs/physical-agents/README.md` and from it
`agent-runtime.md`, `assets.md`, `physics.md`, `scenes.md`, `mlagents-bridge.md`, then
`docs/physical-agents/reports/terrain-walker.md`: that run did for the man on the terrain most of what
you do here for the animals, and its report says what went wrong on the way.

- **The animals** (`src/logic/animals/`): 58 animals, 51 with a walk clip. `analyseGait` reads the
  legs (chain, stance phases) of 39 of them; `proceduralWalk` makes their pose from how they move;
  `gait_dataset` (`-DDYNAMICAL_BUILD_TOOLS=ON`) writes walks drawn from `GaitRanges` as **joint poses
  of the skeleton**, with planted feet and ground heights per frame.
- **The human walker** (`src/logic/agents/humanbot*`): `BodyDef` loaded from
  `resources/agents/bodies/*.body.json` (Unity's frame, converted at load), the systems of
  `common/humanbot_world.json` (`AgentFactory`, `DriveTargets@act`, `PhysicsStep`, `BodySync`, `Task`,
  `Termination`, `Rewards@observe`, `Observation@observe`, `Episode@observe`), the layout
  `heidelbot_notoes_2` (3 header + 9 per joint, two stacked: 138 × 2 observations, 120 actions),
  `AgentIO` and the training host.
- **Demonstrations of a kinematic walk** are recorded today for the man: `walker_demo_gait` and
  `walker_demo_terrain` move the body kinematically and `mode=record-demo` writes what
  `Observation@observe` produces. `tools/train_walker.py --demo-source gait` records, caches and
  trains; `tools/terrain_eval.py` evaluates; `tools/sphere_video.py --scene` films.
- **Terrain for training:** `common/humanbot_terrain_world.json` (`WorldTerrain` colliders,
  `GroundTerrain` field, `WalkRoutes`).
- **Yardstick:** the flat walker trains at about 3000 steps/s (8 environments × 25 worlds) and takes
  off between 5M and 15M steps.

## What is hard, and my defaults

Change a default when you find a reason, and record it in `docs/physical-agents/reports/decisions.md`.

- **Which joints become bodies.** An animal has many more joints than a ragdoll should have bodies:
  toes, ears, tail links, a face, helper bones. Every joint costs the policy observations and actions.
  Default: the legs as `analyseGait` found them (the toes of a foot are one foot), the trunk in two or
  three along the spine, a neck, a head, a tail of one or two bodies when it carries weight: 15 to 25
  bodies. Every other joint rides on its parent.
- **Shapes and mass from the skin.** Fit a capsule or a box to the vertices each body carries, in the
  bind pose; feet get a flat sole. The library has no masses. Default: the volume of the mesh at
  1000 kg/m³, shared between bodies by their volume. Compare what comes out with the real weight of
  five species (bison, red deer, wolf, horse, a man) and put the table in the report; corrections go
  in the overrides file.
- **Joint limits from the motion.** Take each joint's axes and limits from the range it covers over
  all the animal's clips and over the procedural walk across its ranges, plus a margin; a hinge where
  the motion is a hinge. Limits tighter than the demonstrations make them unreachable; 180° everywhere
  makes a body that folds through itself.
- **Drives across three orders of magnitude of mass.** The man's gains are Unity's, for 70 kg. Here
  bodies go from a hare to a rhinoceros. Scale stiffness, damping and torque per joint with what the
  joint carries, by rule. The test of a body, for every animal: with its drives holding the standing
  pose and a zero action it stands 10 s at the 60 Hz step without sinking, shaking or exploding; limp,
  it falls and comes to rest. Small animals may need other solver settings: set them by rule too.
- **The agent runtime is written for one body.** `HumanBot` carries Unity's conventions, a fall test
  on the head above the lower foot, energy and DeVita books. Default: a layout described by the body
  (a header, then the same content per joint as the man's, so the code is shared), one behaviour name
  per species since sizes differ. Generalise the man's systems where that is clean and keeps him
  bit-identical; otherwise write the animal's systems beside them, on the same ragdoll build, drives,
  `AgentIO` and bridge. Leave the energy and DeVita code out.
- **Falling, on four legs.** Default: the episode ends when a body that is not a foot or a lower leg
  touches the ground, or the trunk is under a part of its standing height, measured from the ground
  under it. Check both ways: the limp ragdoll is ended, the kinematic walk across its ranges never is.
- **A demonstration is the observation of the body, not a pose of the skeleton.** Play the procedural
  walk kinematically on the generated body in a world that keeps `Observation@observe`, and record it
  as `walker_demo_gait` does. Then check what the terrain walker's report checked, per animal: limits
  hold on every recorded tick, nothing jumps between two decisions, no body's heading frame flips (the
  man's swing shank did past horizontal; hind legs and necks are the candidates here), no body is
  under the ground, and the speed measured as the reward measures it is the commanded one.
- **The command in the observation must be the one the walk follows.** The man had one speed and a
  target direction redrawn every 400 frames. These demonstrations draw a speed and a turn every
  `segment` seconds. The agent's task (target direction and speed, and when they change) and the
  recorded walk must be the same process in training and in the demonstrations, or GAIL tells them
  apart by the header alone. Speeds are per animal, in speeds of its clip.
- **Ranges: narrow first.** Three range files: `walk_basic` (hard level ground, 0.8 to 1.2 of the
  clip's speed, gentle turns, the default style), `walk_style` (the style parameters wide),
  `walk_soft` (soft ground). Everything gated below runs on `walk_basic`; the others must record.
- **Soft ground has no physics.** The procedural walk wades through a depth; Jolt's ground is rigid.
  Do this last. Default: the hard ground lies `depth` under the surface and what is inside the layer
  is resisted, more sideways than upwards, so that pulling a foot straight out is the cheap way; the
  depth is an episode parameter and in the observation. If time is short, write the design and say it
  is not built.
- **Terrain: level ground first.** Make terrain worlds for the animals that load, spawn on routes and
  record. Measure a slope band (where limits hold and nothing flips) for one quadruped as the terrain
  walker did, not for all.
- **Nobody looks at 39 bodies for you.** Render contact sheets, each body beside its mesh, and look at
  them. Make a view scene where the skinned mesh follows the ragdoll (joints without a body ride on
  their parent with the clip's local pose): fit errors show there, and policies are filmed there.
- **The launcher.** `tools/train_walker.py` is the walker's. Write `tools/train_animal.py --species
  <id|all> --ranges <file> --max-steps <n>`: it generates the body when stale, records the
  demonstration under a key that covers the executable, the body, the ranges, the scenes and the
  params (a stale demo trains silently on other data), and runs the trainer; `all` queues the animals.
  Never edit `start-run.ps1`.
- **The animals the walk does not read.** The 12 of the farm pack (feet held by rig controls): give
  them a body and a demonstration from their walk clip played straight if the same rules can do it,
  else list them as not ready, with the reason. Animals with no walk clip get no agent.
- **Trot and run** go through the same recorder (`gait`). Record one of each and look at it; they are
  not gated.

## Order of work

Each step gates the next. If a gate fails, fix the cause; after two failed attempts at the same gate,
go on with the animals that pass and report the others.

1. Build `RelWithDebInfo` and run the tests (`ctest -LE gpu`): they pass before you change anything.
   `resources/animals/library.json` must be there; if it is not, stop and say so.
2. **Bodies:** the generator, the body files (next to the library, generated and git ignored like
   it), the standing and limp tests for every animal, the contact sheets, the weight table.
3. **The agent:** layout by body, drives, task, fall test, rewards, episodes. The man's golden rollouts
   and tests pass unchanged. A training world of one quadruped steps deterministically.
4. **Demonstrations:** kinematic playback on the body, recording, the checks above, for every animal
   on `walk_basic`.
5. **Scenes and launcher:** train, eval and view scenes on level ground, their terrain variants,
   `training/animal_ppo.yaml` (start from `walker_ppo.yaml`), `tools/train_animal.py`.
6. **Proof on three bodies that differ:** a deer, a two-legged bird and a man of the library, 2M
   steps each: the run starts, both reward streams and the discriminator move, nothing is NaN, steps/s
   are measured.
7. **One quadruped to 20M steps** on level ground. Evaluate it (falls, speed against the command) and
   film it with its skinned mesh. Whether it walks is a result, not a gate: if it does not, say what
   the curves and the video show and what you would change. Restart at most once.
8. **Every animal:** body, recorded demonstration and a 100k-step training that runs through. This
   gives the readiness table.
9. Terrain band for one quadruped, then soft ground, as far as the time allows.

## Rules

- Never ask the user anything. Take the default above, or the simplest option consistent with the
  docs, and log it in `decisions.md`.
- **Work in the main checkout on `master` and commit nothing.** The animal work under it is not
  committed yet and Paul commits it himself: no commit, branch, stash, reset or checkout of files. No
  model name and no "Generated with" line in code or docs.
- Other sessions may be training on this machine. Never stop a process you did not start; say what
  else was running beside any throughput you report. Start long runs so that they outlive your shell
  (`pythonw.exe`, as `tools/train_walker.py`'s docstring says) and wait for them without a tight loop.
- Runs, demonstrations and videos stay under `build/runs`, generated bodies with the library. wandb
  stays offline.
- Follow `CLAUDE.md`: systems and components, scenes in JSON, doctest tests in `tests/unit` (those
  that need the generated library are skipped without it). Write patch scripts to files, not
  heredocs: this shell mangles backslashes and triple quotes.

## What to hand back

`docs/physical-agents/reports/animal-agents.md`, written for someone who has not followed the run:

- The result first: how many animals are ready to train, and the one command that trains one.
- **The readiness table**, a row per animal: legs, bodies and joints, mass, observation and action
  sizes, stands, demonstration recorded and checked, short training ran, and what is missing.
- The proof runs: the three short ones and the 20M one, with curves, the eval and what the video shows.
- What you changed in the engine and why; what is prepared but not validated (terrain, soft ground,
  trot and run); the order in which you would train the animals, and what you expect to break.

Mark what you measured apart from what you infer, and report a failure as plainly as a success: 25
animals ready and 14 listed with their reasons is a result. Update `docs/animals.md` ("Not done") and
the Animals section of `CLAUDE.md`. End your final message with the paths of the report, the contact
sheets, the 20M run's ONNX file and its video.
