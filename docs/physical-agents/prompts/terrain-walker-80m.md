# Autonomous run: an 80M-step walker on the terrain, from the IK walk

Paste everything below the line into a Claude Code session started in a checkout of branch
`agents/ik-gait`, or run it headless:

```
claude -p "$(cat docs/physical-agents/prompts/terrain-walker-80m.md)" --permission-mode acceptEdits
```

---

You are training the physical HumanBot walker of this repository to walk on the marching cubes terrain
(`TerrainGenerator`, what Paul calls the perlin terrain), **fully autonomously**. Nobody answers
questions during this run. The result is one policy trained for 80M steps, measured and filmed.

Three things are fixed by Paul:

1. **The terrain** is the engine's marching cubes world, with its Jolt colliders (`WorldTerrain`), not
   the flat ground prefab and not a height field.
2. **The rewards are the flat walker's, unchanged:** `resources/agents/params/walker.params.json` and
   the environment parameters and reward signals of `training/walker_ppo.yaml` (extrinsic and GAIL at
   strength 1, 1.36 m/s). Do not add terrain, energy or style terms. If the walker fails, the answer is
   in the demonstrations, the terrain it is given or the episode logic, and you report it.
3. **The GAIL demonstrations come from the kinematic IK walk** (`GaitBase`, `GaitFootIK`,
   `GaitBalance`, `Fabrik`), recorded on the terrain. They replace the BasicMotions pose track.
4. **The kinematic walk always goes 1.36 m/s** in everything this run records, on every slope: the
   speed the reward asks for.

## What exists

Read these first: `CLAUDE.md`, `docs/physical-agents/ik-gait.md`, `docs/physical-agents/scenes.md`,
`docs/physical-agents/mlagents-bridge.md`, `docs/physical-agents/reports/P4-P13.md`.

- **The flat walker trains today.** `tools/train_walker.py dynamical` freezes `TrainingEnv.exe`,
  records a demo with `mode=record-demo` on the scene `walker_demo`, and runs the fork's
  `mlagents-learn` in `build/venv`. Scene `walker_train` is 25 `humanbot_world`s on `ground_flat`.
  Reference run `walker_walk136_40m_a`: 40M steps in 3.6 h (about 3000 steps/s, 8 envs × 2 threads),
  0 falls in a deterministic eval, 1.34 m/s. At 9M steps it had an episode length of 860 and a death
  rate of 0.28. Use these as the yardstick for your pilots.
- **The IK walk** runs in `common/ik_world.json` on a `GroundField` (plane or terrain). The spawn has
  `recording: true` and the world keeps `Observation@observe`, so the observation vector is produced,
  but nobody has recorded a `.demo` from it yet. It has no `Task`, `Termination`, `Rewards`, `Episode`
  or `TrackActions`, which `rig_world` (the track demo's world) has.
- **Terrain physics for training exists for the sphere:** `common/sphere_world.json` uses the
  `WorldTerrain` system (seed, margin, below) with no rendering. `ik_check_terrain` and
  `ik_walk_terrain` show the IK side.

## What is hard, and my defaults

Change a default when you find a reason, and record it in `docs/physical-agents/reports/decisions.md`.

- **The terrain is steep and the walker is blind.** Seed 1337 has a median worst slope of 44° over a
  32 m line, and the observation has no terrain in it. The IK walk is realistic up to about 20° along
  and 15° across the path; beyond, its joints leave their limits. So train and record where the ground
  stays in that band: choose spawn points and headings by measured slope along the path the episode can
  cover, not the whole world. Keep the observation and action layout unchanged (138 × 2, 120 actions),
  so the trainer contract and the demos stay compatible. Do not flatten the terrain by changing
  `TerrainGenerator`'s defaults for everyone; a scene parameter is fine if you need one.
- **Everything measured from world y = 0 is wrong on terrain.** Spawn height, the target, the
  fall test (`dyingHeightFactor` times the height), any height in the observation or the stats. Find
  each one and make it relative to the ground under the walker, with flat ground giving bit-identical
  results to before (the golden rollouts in `resources/agents/golden` must still pass).
- **The demonstrations must be poses the physical body can reach.** Record with joint limits that hold
  (`GaitParams::yield_limits` off, or routes where `GaitReport` shows no yield), over many routes and
  headings, uphill, downhill and across, so the discriminator does not learn one hill.
- **The IK walk slows on slopes today, and must not in the demos.** `slope_slowdown`,
  `slope_speed_floor` and the `step_rise` cap take it to 0.79 m/s at 20°. Turn them off through the
  `GaitParams` of the demo scenes (leave the engine defaults and the existing GAIT tests alone), and
  measure speed the way the reward does (read `Rewards`: horizontal or along the path), so the demo's
  speed is 1.36 m/s by the reward's own definition. Add a test that the speed over each stride stays
  within 2 % of it on the slopes you record. At 1.36 m/s a 20° climb needed a 20 cm crouch
  (`ik-gait.md`): where the joint limits cannot hold at that speed, narrow the slope band, do not
  slow the walker.
- **The IK demo must mean the same as the track demo.** Same observation layout, same recording
  conventions (the header, the zeroed speed difference, zero actions). Record an IK demo on flat ground
  and compare it per observation component with the cached `walker_demo` demo before trusting a
  terrain one; explain any component whose range differs.
- **The launcher is built around a pose track.** Its demo arguments and its cache key (track, body,
  layout, decisions) do not cover a gait scene, its routes or `GaitParams`. Extend
  `tools/train_walker.py`; a stale cached demo would silently train on the wrong data. Never edit
  `start-run.ps1`.
- **The ankles' swing limits were widened** in both body files on this branch, so
  `walker_walk136_40m_a` was trained on a slightly different body. Say so wherever you compare with it.

## Order of work

Each step gates the next. If a gate fails, fix the cause; after two failed attempts at the same gate,
stop and report instead of spending the 80M run.

1. Branch `agents/terrain-walker` from `agents/ik-gait`. Build, and run the tests (`ctest -LE gpu`):
   they pass before you change anything.
2. Terrain training world and scenes (train, eval with `Policy@act`, a rendered view scene), with the
   ground-relative fixes. Tests: a kinematic walker and a ragdoll spawned on a slope behave as on flat
   ground with respect to spawn, fall test and target; flat scenes are unchanged.
3. IK demo recording, on flat ground and on terrain, with the comparison above and a check that the
   demo has no jumps between frames.
4. **Pilot A, 5M steps, flat ground, IK demos.** This separates "the IK walk works as GAIL data" from
   "the terrain works". It should track the flat reference at the same step count.
5. **Pilot B, 5M steps, terrain.** Look at the death rate, episode length, both reward streams,
   D(expert) / D(policy) and steps/s. Fix what is broken; do not tune rewards.
6. **The 80M run:** `training/walker_ppo.yaml` copied to a new config with `max_steps` 80000000 and a
   checkpoint every 10M, nothing else changed unless a pilot forced it. Start the launcher so that it
   outlives your shell (`pythonw.exe`, as its docstring says), keep the machine to this one run, and
   wait for it rather than polling in a tight loop. Expect 8 hours or more. Look at each checkpoint;
   restart the run at most once.
7. Evaluate the final policy deterministically, at least 500 episodes: falls, speed, cadence and step
   length, split by slope band (level, up, down, across), and the same on flat ground. Render a video
   of it on the terrain with the capture `sequence` and the ffmpeg of `build/venv`'s imageio-ffmpeg,
   camera as in `GaitView` so the feet stay visible.

## Rules

- Never ask the user anything. Take the default above, or the simplest option consistent with the
  docs, and log it in `decisions.md`.
- Commit on the branch after each step with a plain message. **No `Co-Authored-By`, no "Generated
  with" line, no model name** in commits, code or docs. Do not push and do not open a pull request.
- Runs, demos and videos stay under `build/runs` (not in git). wandb stays offline.
- Follow the code conventions of `CLAUDE.md`: systems and components, scenes in JSON, tests in
  `tests/unit` with doctest.

## What to hand back

`docs/physical-agents/reports/terrain-walker.md`, written for someone who has not followed the run:
the result first (does it walk on the terrain, how well, where it fails), then the eval table by slope
band beside the flat reference, the training curves that matter, what you changed in the engine and
why, and what you would do next. Mark what you measured apart from what you infer. Report a failure as
plainly as a success: a policy that only walks to 10° is a result. End your final message with the
paths of the ONNX file, the video and the report.
