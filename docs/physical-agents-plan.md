# HumanBot physical agents in Dynamical — plan

*2026-11-09 · P0–P3 built. P4 onward is reviewed against the Unity sources
([review](physical-agents/humanbot-port-review.md)) and waits on [open questions](physical-agents/open-questions.md).*

**This engine becomes the new `TrainingEnv.exe`.** It simulates the HumanBot noToes walker as a
**first-class C++ / Jolt implementation** and speaks the stock ML-Agents protocol to the unchanged
Python trainer: the fork, the configs, GAIL and W&B. `start-run` gains an `-Engine dynamical` path.

- **The goal is reproducibility of the experiments, not Unity compatibility.** The gait quality, the
  energetics and the DeVita statistics must be reproduced at matched updates. Unity's arithmetic,
  frames and quirks are not reproduced.
- **Unity keeps authoring.** The prefab and the Mecanim retargeting are exported verbatim to data files,
  and a C++ importer converts them.
- **Every run mode is a scene** of this engine.

## Sources

| Source | Authority on |
|---|---|
| [`physical-agents/unity-analysis.md`](physical-agents/unity-analysis.md) ("UA") | **What the noToes Unity code computes**, with file:line references into `C:\Users\Paul\Desktop\Dynamical-noToes` (branch `slope-lean-devita-on-master` @ `815fb4b41`): body, layouts, task, energy, DeVita, configs, demos, launch contract. Corrects the Unity plan for this body |
| Exported Unity files (`resources/agents/`) | The numbers at runtime: body, params, tracks, golden logs |
| Unity plan `Dynamical/Docs/JoltSim/PLAN.md` (Unity repo, `gladiator`) | Background inventory and decisions D1–D6. Written for the gladiator body and for parity; superseded by UA where they differ |
| The Jolt doc, "Joint Kinetics & Actuator Energy in Jolt Physics" (Paul, 2026-10-29) | Solver settings, the exact energy recorder, the validation identities |
| The ML-Agents fork (release 23, protocol 1.5.0) | Wire format, `.demo` format, ONNX I/O |
| [`physical-agents/`](physical-agents/README.md) | How the engine does it; [`phases.md`](physical-agents/phases.md) is the executable plan |

## Decisions

Numbering follows the Unity plan; D5 (platform: Windows first) is settled by this engine.

| # | Decision | Notes |
|---|---|---|
| D1 | Unity is the training simulator's data source only; the Unity games keep PhysX | |
| D2 | gRPC, protocol 1.5.0, trainer unchanged | Revisit only if the null environment shows a trainer ceiling *and* speed is the goal |
| D3 | **A Jolt-native task definition.** The trained layout is `heidelbot_notoes_2`: orthogonal heading frames, no right-side sign flips, true centre-of-mass velocity, swing·twist targets, finite-difference anchor velocities, reset hazards fixed. Unity's exact math survives only as the test oracle `heidelbot_notoes_1`. The trainer contract (sizes 138 × 2 / 120, behaviour, sensor, stat and env param names, done semantics) is kept exactly | [agent-runtime.md](physical-agents/agent-runtime.md). Costs a new baseline; demos are regenerated from pose tracks anyway |
| D4 | **noToes / Plagenhoef body**: 16 bodies, 15 joints, Plagenhoef masses (sum 99.98 %). Body versions `heidelbot_notoes` (1.89 m) and `heidelbot_notoes_h173` (scaled 0.9153, the DeVita line) | UA §1, §3.6 |
| D6 | Gravity −9.81 | Unity uses −10, including in its energy constants, so those constants are re-derived |
| E1 | **Two executables.** `dynamical` (Vulkan, SDL, OpenXR) and a **headless `dynamical_train`**, shipped as `TrainingEnv.exe`. Both link one `dynamical_sim` library | [architecture.md](physical-agents/architecture.md) |
| E2 | **Scenes.** Every way of running the engine is a JSON scene that declares its variables, systems, worlds and content | [scenes.md](physical-agents/scenes.md) |
| E3 | **Worlds.** A training process holds N independent worlds (25 by default, as Unity's 5 × 5 PrefabGrid), each with its own `entt::registry`, `SystemSet` and Jolt `PhysicsSystem`. Taskflow steps them in parallel, each single-threaded inside | [simulation.md](physical-agents/simulation.md) |
| E4 | **Flat ground only** until the walker trains (M6): a static box with its top at y = 0. Slope arrives in M7; the lean line, which trains on a fixed 10° slope, waits for it | [physics.md](physical-agents/physics.md#ground) |
| E5 | **One frame: the engine's** (right-handed, y-up, −z forward). Unity coordinates exist only in the files and the C++ importer, which mirrors z once at load | [conventions.md](physical-agents/conventions.md) |
| E6 | **The viewer is a separate process.** `dynamical scene=walker_view` watches the results folder and hot-loads the newest ONNX | [inference-and-replay.md](physical-agents/inference-and-replay.md) |
| E7 | **One exact energy channel** from Jolt constraint lambdas (`energySource` 3). It replaces Unity's channels 0–2, Full GRF and the charge residual; their names are accepted and mapped, with a warning | [physics.md](physical-agents/physics.md#energy) |

## The scenes

| Scene | Executable | Purpose |
|---|---|---|
| `base` | `dynamical` | Today's master: marching cubes world, grass, glTF, Jolt boxes, VR. Must be unchanged by the scene port |
| `rig_anim` | `dynamical` | **Kinematic rig validation.** Plays Unity pose tracks onto the ragdoll's kinematic bodies and draws them, with live checks (rest pose, limits, bone lengths, velocities, ground, observation vector). [rig-animation.md](physical-agents/rig-animation.md) |
| `walker_train` | `dynamical_train` | N flat-ground worlds, one agent each, the training system list only |
| `walker_view` | `dynamical` | One or more flat-ground worlds driven by the latest exported ONNX, drawn with panels |

`dynamical_train` also has the modes `null` (M0), `bench`, `eval` and `record-demo`, on the same
scene files.

## The two experiment lines (UA §9.1)

| | Flat GAIL walker (first) | DeVita energetics (M7) |
|---|---|---|
| Body | `heidelbot_notoes`, 70 kg | `heidelbot_notoes_h173`, 69 kg |
| Behaviour | `HeidelBotBehavior` | `AnySpeedBotBehavior` |
| Rewards | root 0.34, death −10, GAIL 1 | energy (exact channel), death −10, GAIL 0.5 |
| Demo | `BasicMotions@Walk`, 3000 decisions | `Walk1p500mps-h173`, 10000 decisions |
| Ground | flat | flat, then slope ±10° |

## What master already has

| Need | Master | Change |
|---|---|---|
| Jolt | `PhysicsWorld`, `PhysicsSys` (60 Hz accumulator, box and sphere bodies, interpolation), `TerrainColliders`. Jolt 5.5 via vcpkg | **Jolt 5.6.0 through an overlay port** (S2), with patches P-A and P-C (M2); no vcpkg baseline bump. Registration moves into `JoltRuntime`. The world gets a config and lives in the registry ctx. `PhysicsSys` splits into reusable systems |
| ECS | EnTT 3.16, `System` / `SystemSet` with pre and post lists | Add a fixed lane with act and observe phases, parking, and name-based system and component registries |
| Rendering | `ModelPass` draws `Transform` + `ModelRenderable` glTF instances; four passes hard-coded; no capture | Pass list from the scene; capture readback (P0); `WorldView` with primitives and generated capsules. No new pipeline for v1 |
| Settings | cereal JSON with 9 known `key=value` keys; writes `config.6.json` at every start | `Settings` stays for `dynamical`. A new `RunArgs` parses scene, process, capture and trainer arguments; `dynamical_train` never touches `config.6.json` |
| Build | One globbed executable, no tests | Split into `dynamical_sim`, `dynamical`, `dynamical_train`, `dynamical_mlagents`, `dynamical_onnx`, `dynamical_tests`, `imgdiff`; CI on Windows without a GPU |

## Milestones

The executable plan is [`physical-agents/phases.md`](physical-agents/phases.md) (P0–P15), with ordered
steps, automated tests with thresholds, and the visual sign-off per phase. The table below is the
summary, in execution order.

- P0 (the test and capture harness) comes before S1.
- **M5 (demos) comes before M4 (bridge)**, so M4 can gate on an unmodified GAIL config.
- Gates are measured with behavioural stats at matched gradient updates, never mean reward.

| Phase | # | Engine work | Unity-side work | Gate |
|---|---|---|---|---|
| P0 | **Harness** | Tests target, capture, `imgdiff`, phase report, probes, CI; master baseline recorded | — | Captures deterministic; master goldens and probe committed |
| P1 | **S1** Scenes | Scene loader, variables, registries, prefabs, fixed lane, renderer pass list, `RunArgs`. CMake split | — | `base` matches master (probe ≤ 1e-5 m, captures within `imgdiff`). Malformed scenes fail with file and JSON path. `dynamical_train` links no Vulkan, SDL or OpenXR |
| P2 | **S2** Worlds | **Jolt 5.6.0 overlay**, `JoltRuntime`, `PhysicsConfig`, `PhysicsSys` split, `World`, `WorldStepper`, `WorldView`, flat `Ground` | — | 25 worlds × 50 boxes at rest; determinism hash equal for 1 and 8 threads; drawn worlds match their golden |
| P3 | **M0** Null env | `dynamical_mlagents`, `null` scene, CTRL_C, log file sink | Freeze the reference runs (flat walker, DeVita H173) | Trainer steps/s at 6 and 20 envs. **About 424 means the trainer is the ceiling** |
| P4–P5 | **M1** Body + rig | Unity importer, `RagdollBuilder`, `BodySync`, `TrackActions`, `RigChecks`; **`rig_anim`** | Exporter (PhysX readback, params), pose-track bake, Unity reference screenshots | Pre-knee-move rest pose equals Unity's to 1e-5 m; mass properties equal the readback; walk tracks play with the reviewed limit excursions; side-by-side signed off |
| P6–P7 | **M2** Joints + energy | Limits, `PositionAndVelocity` motors, gain-scale fit, **patches P-A and P-C**, contacts, friction combine, exact energy recorder | Limit-sweep rig, drive step responses | Envelopes within 1°; step responses within 15 %; stands for 10 s; energy closure ≤ 2 % |
| P8 | **M3** Agent runtime | `heidelbot_notoes_2`, the `_1` oracle, task systems, `walker_heuristic` | Golden rollouts | `_1` reproduces Unity's observations to 1e-4, and rewards and kills match the logs; `_2` passes its `[task]` tests |
| P9 | **M5** Demos | `mode=record-demo` (`walker_demo`), `.demo` writer, cache | Bake the walk tracks | `demo_loader` accepts the files; spec matches; header x,z zero |
| P10 | **M4** Bridge | Full bridge over N worlds, env params, `envparams.txt` with hashes, stats, `start-run -Engine dynamical` | `start-run` engine path | **Unmodified** `default-training.yaml` (GAIL on) trains 1M steps through `start-run`; Unity's stat keys ⊆ the engine's; a lesson change resets cleanly; `stop-run` exports the ONNX |
| P11 | **M5b** Viewer | `dynamical_onnx`, `Policy`, `ModelWatcher`, **`walker_view`**, minimal `mode=eval` | — | `PolicyRunner` equals Python onnxruntime to 1e-5; the viewer swaps checkpoints during a run |
| P12 | **M6** First walker | Train the flat GAIL walker from scratch on `_2`, 3 seeds | — | Dead rate, episode length, root speed vs target and head height reach the frozen Unity reference at matched updates. Paul judges the video |
| P13 | **M7** Energetics, slope | DeVita H173 line on the exact channel, `DeVitaStepStat`, `SlopeGround`, `energyScale` re-derived | — | Synthetic DeVita tests exact; reward equals the Python reference; DeVita keys produced; neg/pos ratio reported (a finding, not a gate) |
| P14 | **M8** Eval, replay | Full eval (Unity CSV and `EVT|`), replays | `ReplayPlayer` scene | `energy_eval_run.py` and `gait_run.py` run unchanged; replay equals live bit-exactly |
| P15 | **M9** Extended | Lean line, body variants, strike, duel, heightfield terrain; policy walkers in `base` | Variant exports | Each gated like M3 + M6 |

M5b needs the M3 layout and an exported ONNX (M4). It can run alongside M6.

## Risks

- **Throughput.** Unity measured about 424 steps/s at 20 envs at low CPU and GPU, so the limit may be
  the trainer. M0 settles it.
- **Drive semantics.** Swing-twist motors are not a SLERP drive. Budget for the gain-scale fit (M2) and
  a tuning iteration in M6.
- **Feet.** The friction model (5.6: per-manifold with torsion), speculative contacts and the foot boxes
  decide stance. Compare drop, slide and stance-duration stats early.
- **A new layout means a new baseline.** `_2` checkpoints never compare to Unity ones except
  behaviourally at matched updates.
- **The GAIL header coupling** (UA §6): demos zero the velocity header, so GAIL rewards command
  tracking. `_2` keeps the coupling on purpose.
- **The Unity baseline trained on spiked demos** (UA §6, review R4): joint speeds up to 28 km/s reach the
  discriminator raw at every other demo episode start. Engine demos have none, so P12's comparison carries that
  confound.
- **No flat-walker reference run exists** for the noToes body (open-questions Q2). It gates P12.
- **Jolt patch maintenance.** P-A and P-C live in an overlay port on 5.6.0. Upstream them; re-verify on
  every Jolt bump.
- **The scene port could regress `base`.** P0 pins master's behaviour; P1 gates on it.
- **Jolt global state.** Every test and both executables go through `JoltRuntime`.
- **The Unity working tree is dirty**: uncommitted energy changes (UA, status). The contract is what is
  committed.
- **Unity-side tooling** (exporter, bake, limit sweep, step responses, golden logger, `start-run` engine
  path) is real work in the Unity repo, on the critical path of P4–P10.
