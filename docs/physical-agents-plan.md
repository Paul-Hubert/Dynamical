# HumanBot physical agents in Dynamical — plan

*2026-10-30 · Plan only; nothing is built.*

**This engine becomes the new `TrainingEnv.exe`.**
- It simulates HumanBot ragdolls on Jolt and speaks the stock ML-Agents protocol to the unchanged
  Python trainer: the fork, the configs, GAIL, W&B and `start-run`.
- Unity keeps authoring: the prefab and the Mecanim retargeting, exported to data files.
- Everything Unity does at runtime for training is rebuilt here, as **scenes** of this engine.

## Sources

| Source | Authority on |
|---|---|
| **Unity plan**: `Dynamical/Docs/JoltSim/PLAN.md` in the Unity repo (`C:\Users\Paul\Desktop\Dynamical`, branch `gladiator` @ `d762114ec`) | What has to be rebuilt: body, physics behaviour, observation and action contracts, tick order, episodes, rewards, energy, bridge behaviour, demos, tooling, decisions D1–D6, Unity-side milestone work. Cited below as "UP §x" |
| **The Jolt doc**: "Joint Kinetics & Actuator Energy in Jolt Physics" (Paul, 2026-10-29, claude.ai artifact) | Solver settings, the exact energy recorder, the validation identities |
| **The ML-Agents fork** (release 23, protocol 1.5.0) | Wire format, `.demo` format, ONNX I/O |
| **This repo** | How the engine does it: [`physical-agents/`](physical-agents/README.md) |

The engine docs never restate a Unity number that UP owns. They say where it plugs in and which test
checks it.

## Fixed decisions

| # | Decision | Notes |
|---|---|---|
| D1 | Unity is the training simulator's data source only; the Unity games keep PhysX | UP §5 |
| D2 | gRPC, protocol 1.5.0, trainer unchanged | Revisit only if the null environment shows a trainer ceiling *and* speed is the goal |
| D3 | Keep the observation layout and parity-test it; fix the scheduling quirks by design | UP §3.6 |
| D4 | **noToes / Plagenhoef body**: 16 bodies, 15 joints (no toe joints), Plagenhoef anthropometric masses, obs 138 × 2, 120 actions. Source: the `Dynamical-noToes` worktree (`C:\Users\Paul\Desktop\Dynamical-noToes`, branch `slope-lean-devita-on-master` @ `815fb4b41`, based on Unity `master`) | Absolute J/(kg·m) for energetics needs anthropometric masses (UP D4). The engine stays body-agnostic: the body file and a versioned layout decide, so the gladiator body remains exportable later |
| D6 | Gravity −9.81 | |
| E1 | **Two executables.** `dynamical` (Vulkan, SDL, OpenXR) and a **headless `dynamical_train`**, shipped as `TrainingEnv.exe`. It links no graphics, and both link one `dynamical_sim` library | [architecture.md](physical-agents/architecture.md) |
| E2 | **Scenes.** Every way of running the engine is a JSON scene that declares its variables, systems, worlds and content | [scenes.md](physical-agents/scenes.md) |
| E3 | **Worlds.** A training process holds N independent worlds, each with its own `entt::registry`, `SystemSet` and Jolt `PhysicsSystem`. Taskflow steps them in parallel, each single-threaded inside | Replaces Unity's 5×5 `PrefabGrid` with collision filtering. [simulation.md](physical-agents/simulation.md) |
| E4 | **Flat ground only** for training until the walker trains (M6): a static box with its top at y = 0. Slope (`SlopeGround`) arrives in M7, heightfields after | [physics.md](physical-agents/physics.md#ground) |
| E5 | **Sim frame = Unity's numbers** (left-handed, y-up, +Z forward). The engine is right-handed, y-up, −Z forward. Drawing mirrors z; nothing else converts | [conventions.md](physical-agents/conventions.md) |
| E6 | **The viewer is a separate process.** `dynamical scene=walker_view` watches the results folder and hot-loads the newest ONNX. A renderer crash cannot stop a run | [inference-and-replay.md](physical-agents/inference-and-replay.md) |

## The scenes

| Scene | Executable | Purpose |
|---|---|---|
| `base` | `dynamical` | Today's master: marching cubes world, grass, glTF, Jolt boxes, VR. Must be unchanged by the scene port |
| `rig_anim` | `dynamical` | **Kinematic rig validation.** Plays Unity pose tracks onto the ragdoll's kinematic bodies and draws them. Live checks: rest pose, joint angles against limits, finite-difference velocities, observation vector. [rig-animation.md](physical-agents/rig-animation.md) |
| `walker_train` | `dynamical_train` | N flat-ground worlds, one agent each. The training system list only: no terrain, no chunks, no input, no UI |
| `walker_view` | `dynamical` | One or more flat-ground worlds driven by the latest exported ONNX, drawn with panels |

`dynamical_train` also has the modes `null` (M0), `bench`, `eval` and `record-demo`, on the same
scene files.

## What master already has

| Need | Master | Change |
|---|---|---|
| Jolt | `PhysicsWorld`, `PhysicsSys` (60 Hz accumulator, box and sphere bodies, interpolation), `TerrainColliders`. Jolt 5.5 via vcpkg | Jolt registration moves out of `PhysicsWorld` into a process guard, because a second world would unregister the types. The world gets a config (single-threaded job system for training) and moves into the registry ctx. `PhysicsSys` splits into reusable systems |
| ECS | EnTT 3.16, `System` / `SystemSet` with pre and post lists | Add a fixed lane and decision-tick systems. Add name-based system and component registries for scenes |
| Rendering | `ModelPass` draws `Transform` + `ModelRenderable` glTF instances | Body segments are drawn as glTF box and capsule instances in the host registry. No new pipeline for v1 |
| Settings | cereal JSON + `key=value` arguments | Unchanged. Scenes use nlohmann-json, and `dynamical_train` also parses the Unity-style flags the trainer passes |
| Build | One globbed executable | Split into `dynamical_sim`, `dynamical`, `dynamical_train`, `dynamical_mlagents`, `dynamical_onnx` and `dynamical_tests` |

## Milestones

The gates in the last column are measured: behavioural stats at matched gradient updates, never mean
reward (UP §4). Steps S1–S3 are engine work; M0–M9 follow UP §4, with the engine tasks listed per row.

| # | Engine work | Unity-side work (UP §4) | Gate |
|---|---|---|---|
| **S1** Scenes | Scene loader, variables, system and component registries, prefabs, fixed lane, optional renderer with a pass list. CMake split into a library and two executables; `dynamical_tests` | — | `base` runs identically to master. Malformed scenes fail with the file and JSON path. `dynamical_train` builds and links no Vulkan, SDL or OpenXR |
| **S2** Worlds | `JoltRuntime`, `PhysicsConfig`, `PhysicsSys` split, `World`, `WorldClock`, `WorldRng`, `WorldStepper`, `WorldView`, flat `Ground` | — | 25 worlds × 50 falling boxes step in parallel in `dynamical_train mode=bench`; a determinism hash is equal across two runs; `dynamical` draws one world |
| **M0** Null env | `dynamical_mlagents`: handshake, step loop, side channels; `mode=null` with fixed observations for 25 agents | Freeze the reference runs (one GAIL walker, one DeVita) | Trainer steps/s at 6 and 20 envs. **About 424 means the trainer is the ceiling** |
| **M1** Body + rig | Body file and pose-track loaders, `RagdollBuilder`, `BodySync`; **`rig_anim` scene** | Exporter and pose-track bake | Rest pose equals Unity's bone world poses to 1e-5 m, joint angles read 0 at rest, and the mass properties equal the play-mode readback. `rig_anim` plays the walk track with no joint outside its limits |
| **M2** Joints | Limits, motors, contacts, friction combine, energy recorder, Joints and Energy panels | Limit-sweep rig, drive step responses | Envelopes within 1° (twist sign, elliptical cone). Documented step-response match after gain re-derivation. Passive drop without explosion. Jolt doc checks 1–3 |
| **M3** Agent runtime | Observations, actions, rewards, termination, episodes and stats in parity mode; golden tests. `rig_anim` shows the live observation diff | Per-tick logs from Unity rollouts | Observations equal Unity's to 1e-4 on identical poses. Reward and termination tests reproduce the logs. The action mapping round-trips |
| **M4** Bridge | Full bridge over N worlds, env params, `envparams.txt`, stats names, CTRL_C, `TrainingEnv.exe` launch contract | — | An unmodified config trains 1M steps. `start-run` prints `STARTED`. The W&B key set equals a Unity run's. A lesson change resets cleanly. `stop-run` exports the ONNX |
| **M5** Demos | `mode=record-demo` from pose tracks, `.demo` writer, cache | Bake the walk clip(s) | `demo_loader` accepts the file; the observation spec matches; the differences from a Unity demo are explained |
| **M5b** Viewer | `dynamical_onnx`, `PolicySys`, `latest:` model source, **`walker_view` scene** | — | During a run, the viewer shows the newest checkpoint and swaps models without restarting |
| **M6** First walker | Train the GAIL flat walker from scratch | — | Dead rate, episode length, root speed vs target and head height reach the M0 reference at matched updates. Paul judges the video |
| **M7** Energy, slope | Exact energy channel (new `energySource` value), `DeVitaStepStat`, `SlopeGround` (tilted box), physics rate | — | Jolt doc validation on eval episodes; DeVita stats produced; `energyScale` re-derived |
| **M8** Eval, replay | `mode=eval` with the same CSV and `EVT|` lines, replay writer, replay playback in `walker_view` | `ReplayPlayer` scene for videos | `energy_eval_run.py` and `gait_run.py` run unchanged; post-training video from a replay |
| **M9** Extended | UP §4 M9 order (body variants, strike, duel, heightfield terrain, soil…); then policy walkers in `base` on marching-cubes colliders | Variant exports | Each gated like M3 + M6 |

M5b depends only on M4, because an exported ONNX is all it needs. It can move ahead of M5.

## Risks

UP §6 applies unchanged: throughput, drive semantics, feet, unknown PhysX behaviour, two lines in
flight, and unverified Jolt API claims. Engine-specific risks:

- **The scene port regresses `base`.** S1 gates on identical behaviour. Port `Game::start` mechanically
  before adding features.
- **Jolt's global state.** Registration, the factory and the default allocator are process-wide. Every
  test and both executables must go through `JoltRuntime`.
- **vcpkg Jolt flags.** Cross-platform determinism and assert settings must match between the port and
  the consumer. Windows-only training first (UP D5) lowers the stakes.
