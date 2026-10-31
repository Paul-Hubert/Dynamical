# Physical agents — engine docs

How the HumanBot ragdolls, trained with ML-Agents in Unity today, are rebuilt in Dynamical: Jolt
worlds, a JSON scene system, a headless training executable that the unchanged trainer launches as
`TrainingEnv.exe`, and render scenes for rig validation and for watching policies.

*Status 2026-10-31: design only.* The overview, decisions and milestones are in
[`../physical-agents-plan.md`](../physical-agents-plan.md).

## Reading order

| # | Doc | Covers |
|---|---|---|
| 1 | [architecture.md](architecture.md) | Targets, library boundary, directories, CMake, dependencies, threading |
| 2 | [conventions.md](conventions.md) | One engine frame and the Unity importer, units, time, ids, code style |
| 3 | [scenes.md](scenes.md) | Scene files, variables, registries, prefabs, the four scenes |
| 4 | [simulation.md](simulation.md) | Worlds, fixed lane, per-tick schedule, determinism, the training host |
| 5 | [physics.md](physics.md) | Changes to master's Jolt code, layers, ragdoll build, joints, drives, contacts, flat ground, energy |
| 6 | [assets.md](assets.md) | Body file, pose tracks, agent parameters, Unity exporter contract |
| 7 | [rig-animation.md](rig-animation.md) | The `rig_anim` kinematic validation scene |
| 8 | [agent-runtime.md](agent-runtime.md) | Observations, actions, rewards, episodes, randomisation, termination, stats |
| 9 | [mlagents-bridge.md](mlagents-bridge.md) | Launch contract, handshake, step loop, side channels, env params, process behaviour |
| 10 | [inference-and-replay.md](inference-and-replay.md) | ONNX Runtime, `walker_view`, eval mode, demo recorder, replays |
| 11 | [testing.md](testing.md) | Test target, oracle tests, gates, benchmarks |
| — | [jolt-readouts.md](jolt-readouts.md) | **Reference**: Jolt 5.5 constraint and contact impulse access, drive semantics, the three patches |
| — | [unity-analysis.md](unity-analysis.md) | **Reference**: what the noToes Unity code computes, with file:line sources |
| 12 | [diagram.md](diagram.md) | Spec and Claude Design prompt for the implementation-plan diagram |

## Rules for these docs

- **Jolt-native first.** These docs define a C++ / Jolt implementation that reproduces the experiments'
  behaviour. Unity compatibility is not a goal; the trainer contract is.
- **The exports own the numbers at runtime.** Where a doc quotes a Unity value, the source is
  [unity-analysis.md](unity-analysis.md) ("UA §x"), which carries file:line references into the noToes
  Unity tree. The older Unity plan (`Dynamical/Docs/JoltSim/PLAN.md`, gladiator) is background only.
- **Mark unverified protocol details** *verify*. They were written from memory of upstream ML-Agents;
  check each against the fork before implementing.
- **Master is the baseline.** File and type names refer to the current `master` (`src/physics/`,
  `src/logic/`, `src/renderer/`). The archived tile-map engine (`archive/tilemap/`) is not involved.

## Glossary

| Term | Meaning |
|---|---|
| World | One independent simulation: an `entt::registry`, its `SystemSet` and its Jolt `PhysicsSystem`. A training *area* in ML-Agents terms |
| Host registry | The process-wide registry (`Game::registry`, or `TrainHost`'s): settings, executor, scene variables, bridge, renderer state |
| Sim frame | Unity's coordinates, used unchanged in physics, observations and assets |
| Body file | Exported prefab physics: segments, shapes, mass properties, joints, rest pose |
| Pose track | Exported per-tick world poses of every segment, baked from a retargeted Unity clip |
| Decision tick | The physics tick on which observations go out and actions come in |
| Layout | Versioned C++ definition of the observation and action vectors for one body |
