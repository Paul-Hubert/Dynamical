# Physical agents — engine documentation

This folder specifies how the HumanBot DRL characters (Jolt ragdolls trained with ML-Agents) are ported
into Dynamical: an EnTT simulation, a Vulkan renderer, and a headless training executable that the
unchanged ML-Agents trainer drives over gRPC.

*Status 2026-10-30: design only. Nothing in this folder is built yet.*

## Sources of truth

| Topic | Authority |
|---|---|
| Body, joint drives, observation layout, actions, rewards, terminations, randomisation, stats, energy, demos | Unity repo `Dynamical/Docs/JoltSim/PLAN.md` (the "Unity plan"). These docs never restate its numbers; they say where each one plugs in |
| Wire protocol, `.demo` format, ONNX export | The ML-Agents fork the trainer runs (`ml-agents-envs/mlagents_envs/communicator_objects/*`, `demo_loader.py`, `torch_utils/model_serialization.py`). Protocol **1.5.0** |
| Engine structure | This folder |
| Overview and decision log | [`../physical-agents-plan.md`](../physical-agents-plan.md) |

Whenever a doc here gives a byte layout or enum value from ML-Agents, it was written from memory of
the upstream code. **Check it against the fork before implementing**; each such place is marked
*verify*.

## Reading order

| # | Doc | Covers |
|---|---|---|
| 1 | [architecture.md](architecture.md) | Targets, libraries, directories, CMake, dependencies, threading, arenas |
| 2 | [conventions.md](conventions.md) | Frames, units, handedness, ids, time, naming |
| 3 | [simulation-loop.md](simulation-loop.md) | Fixed step, `SystemSet` changes, decision period, per-tick order, headless host |
| 4 | [physics.md](physics.md) | Jolt setup, `PhysicsWorld`, layers, ragdoll build, joint limits and drives, contacts, ground, energy |
| 5 | [body-and-assets.md](body-and-assets.md) | Body file, scenario file, pose tracks, the Unity exporter contract |
| 6 | [agent-runtime.md](agent-runtime.md) | Agent components and systems: actions, observations, rewards, episodes, randomisation, stats |
| 7 | [mlagents-bridge.md](mlagents-bridge.md) | gRPC client, handshake, step exchange, side channels, launch arguments |
| 8 | [demo-recorder.md](demo-recorder.md) | Writing `.demo` files from pose tracks |
| 9 | [inference.md](inference.md) | ONNX Runtime for eval mode and game agents |
| 10 | [rendering-and-tools.md](rendering-and-tools.md) | Debug draw, skinned mesh, replay, video capture, ImGui panels |
| 11 | [game-integration.md](game-integration.md) | Physical humans in the colony game, the time-scale problem, LOD |
| 12 | [testing.md](testing.md) | Test target, golden parity tests, performance gates |
| 13 | [milestones.md](milestones.md) | M0–M9 task lists and gates |

## One-paragraph summary

A new static library `dynamical_sim` holds everything that does not need a window: the map, ECS
components, AI, Jolt physics and the agent runtime. The game executable `dynamical` links it plus the
renderer and UI. A new executable `dynamical_train` links it plus a gRPC/protobuf ML-Agents client and
nothing graphical. Each training **arena** is its own `entt::registry` with its own `SystemSet` and
its own Jolt `PhysicsSystem`; arenas are stepped in parallel with Taskflow, each single-threaded inside.
Physics runs at a fixed step; every `decision_period` ticks the bridge gathers `AgentInfoProto`s from
all arenas, exchanges one message with the Python trainer, and scatters the actions back. The
simulation keeps Unity's numbers (left-handed, y-up, metres) so observation golden tests against Unity
remain possible; the only frame conversion happens when drawing into the z-up map.
