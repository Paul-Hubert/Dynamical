# HumanBot physical agents in Dynamical — plan

*2026-10-31 · Plan only; nothing is built.*

**This engine becomes the new `TrainingEnv.exe`.** It simulates the HumanBot noToes walker as a
**first-class C++ / Jolt implementation** and speaks the stock ML-Agents protocol to the unchanged
Python trainer: the fork, the configs, GAIL, W&B and `start-run`.

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
| [`physical-agents/`](physical-agents/README.md) | How the engine does it |

## Decisions

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
| Jolt | `PhysicsWorld`, `PhysicsSys` (60 Hz accumulator, box and sphere bodies, interpolation), `TerrainColliders`. Jolt 5.5 via vcpkg | Registration moves out of `PhysicsWorld` into a process guard. The world gets a config and lives in the registry ctx. `PhysicsSys` splits into reusable systems |
| ECS | EnTT 3.16, `System` / `SystemSet` with pre and post lists | Add a fixed lane with act and observe phases, plus name-based system and component registries |
| Rendering | `ModelPass` draws `Transform` + `ModelRenderable` glTF instances | Body segments are drawn as glTF box and capsule instances. No new pipeline for v1 |
| Settings | cereal JSON + `key=value` arguments | Unchanged. Scenes use nlohmann-json; `dynamical_train` also parses the trainer's Unity-style flags |
| Build | One globbed executable | Split into `dynamical_sim`, `dynamical`, `dynamical_train`, `dynamical_mlagents`, `dynamical_onnx` and `dynamical_tests` |

## Milestones

Gates are measured with behavioural stats at matched gradient updates, never mean reward.

| # | Engine work | Unity-side work | Gate |
|---|---|---|---|
| **S1** Scenes | Scene loader, variables, registries, prefabs, fixed lane, optional renderer with a pass list. CMake split; `dynamical_tests` | — | `base` runs identically to master. Malformed scenes fail with the file and JSON path. `dynamical_train` links no Vulkan, SDL or OpenXR |
| **S2** Worlds | `JoltRuntime`, `PhysicsConfig`, `PhysicsSys` split, `World`, `WorldClock`, `WorldRng`, `WorldStepper`, `WorldView`, flat `Ground` | — | 25 worlds × 50 falling boxes in `dynamical_train mode=bench`; determinism hash equal across runs; `dynamical` draws one world |
| **M0** Null env | `dynamical_mlagents`: handshake, step loop, side channels; `mode=null` with 25 agents | Freeze the reference runs (flat walker, DeVita H173) | Trainer steps/s at 6 and 20 envs. **About 424 means the trainer is the ceiling** |
| **M1** Body + rig | Exporter-format loaders, **Unity importer**, `RagdollBuilder`, `BodySync`; **`rig_anim`** | Exporter (body with PhysX readback, params), pose-track bake | The imported rest pose equals Unity's to 1e-5 m; masses and inertia equal the readback; `rig_anim` plays the walk tracks with limit excursions reported |
| **M2** Joints + energy | Limits, motors, gain-scale derivation, contacts, friction combine, exact energy recorder, panels | Limit-sweep rig, drive step responses | Envelopes within 1°; step responses matched after the scale is derived; passive drop without explosion; energy closure < 1–2 % |
| **M3** Agent runtime | `heidelbot_notoes_2` and the `_1` oracle; rewards, termination, reset, disturbances, stats | Per-tick logs from Unity rollouts (golden files) | The `_1` oracle reproduces Unity's observations to 1e-4, and the rewards and kills match the logs. This validates the rig and features. `_2` passes its `[task]` tests |
| **M4** Bridge | Full bridge over N worlds, env params (UA §7 union), `envparams.txt` with sha, stats names, CTRL_C, launch contract | — | An unmodified config trains 1M steps; `start-run` reaches its ready marker; the W&B key set equals a Unity run's; a lesson change resets cleanly; `stop-run` exports the ONNX |
| **M5** Demos | `mode=record-demo`, `.demo` writer, cache | Bake the walk tracks | `demo_loader` accepts the files; the spec matches; header x,z zero |
| **M5b** Viewer | `dynamical_onnx`, `PolicySys`, `latest:` source, **`walker_view`** | — | During a run, the viewer shows the newest checkpoint and swaps models without restarting |
| **M6** First walker | Train the flat GAIL walker from scratch on `_2` | — | Dead rate, episode length, root speed vs target and head height reach the frozen Unity reference at matched updates. Paul judges the video |
| **M7** Energetics, slope | DeVita H173 line: energy reward on the exact channel, `DeVitaStepStat`, `SlopeGround`, `energyScale` re-derived for g = 9.81 | — | Jolt doc validation on eval episodes; DeVita stats produced; neg/pos ratio reported (a finding, not a gate) |
| **M8** Eval, replay | `mode=eval` with the same CSV and `EVT|` lines, replay writer, replay playback in `walker_view` | `ReplayPlayer` scene for videos | `energy_eval_run.py` and `gait_run.py` run unchanged; post-training video from a replay |
| **M9** Extended | Lean line, body variants, strike, duel, heightfield terrain, soil; then policy walkers in `base` | Variant exports | Each gated like M3 + M6 |

M5b depends only on M4, because an exported ONNX is all it needs. It can move ahead of M5.

## Risks

- **Throughput.** Unity measured about 424 steps/s at 20 envs at low CPU and GPU, so the limit may be the
  trainer. M0 settles it.
- **Drive semantics.** Swing-twist motors are not a SLERP drive. Budget for gain-scale derivation (M2)
  and a tuning iteration in M6.
- **Feet.** The friction combine, speculative contacts and the foot boxes decide stance. Compare drop,
  slide and stance-duration stats early.
- **A new layout means a new baseline.** `_2` checkpoints never compare to Unity ones except
  behaviourally at matched updates.
- **The GAIL header coupling** (UA §6): demos zero the velocity header, so GAIL rewards command
  tracking. `_2` keeps the coupling on purpose.
- **Unverified Jolt API.** Per-sub-step lambda access and post-solve contact impulses. Fallbacks:
  stepping ourselves, and the momentum balance.
- **The scene port could regress `base`.** S1 gates on identical behaviour; port `Game::start`
  mechanically first.
- **Jolt global state.** Every test and both executables go through `JoltRuntime`.
- **The Unity working tree is dirty**: uncommitted energy changes (UA, status). The contract is what is
  committed.
