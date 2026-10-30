# Physical HumanBot agents in the Dynamical engine — plan

**The HumanBot training simulation is built inside this engine rather than as a standalone program.**
The Python trainer stays unchanged: the ML-Agents fork, its configs and GAIL. This engine replaces the
Unity player, and Unity is kept only as the source of the body and motion data.

- **Full inventory:** the Unity-side inventory of what has to be rebuilt is in the Unity repo at
  `Dynamical/Docs/JoltSim/PLAN.md`. It covers the body, joint drives, observations, actions, rewards,
  the tick order, the ML-Agents protocol, demos and tooling, and it still applies.
- **This document** only maps that plan onto this engine.
- **Detailed engine docs:** [`physical-agents/`](physical-agents/README.md) — architecture, conventions,
  simulation loop, Jolt physics, assets, agent runtime, ML-Agents bridge, demos, inference, rendering,
  game integration, testing and per-milestone task lists.

*2026-10-30 · Plan only; nothing built.*

---

## What the engine already provides

| The Unity plan needs | This engine has | Used for |
|---|---|---|
| An ECS | `entt::registry` and `System`/`SystemSet` | Arena, agent, body-segment and joint entities. Physics and agent systems in `pre_tick` |
| Parallel arenas | Taskflow executor | Stepping the arenas and agents in parallel, alongside Jolt's job system |
| Body, scenario and parameter files | `Settings` with cereal JSON | Body file, scenario file, env-param defaults |
| Terrain | Chunked tile map with heights, FastNoise2 | Heightfield ground for slope and terrain experiments, replacing Unity's `TerrainData` and `Mathf.PerlinNoise` |
| Rendering for videos | Vulkan renderer with compute | Agent viewer, replay playback and video capture, replacing the Unity video builds |
| Inspection | ImGui, ImPlot, `DevMenuSys` | Live panels for joint angles, torques, energy channels and rewards |
| Profiling | Optick | The step-loop cost per system |
| Build | CMake + vcpkg, Visual Studio 2022 and NixOS | Add Jolt, gRPC/protobuf and ONNX Runtime |
| A high-level decision layer | Score-based behaviours and action chains, plus the LLM planner design (`llm-ai-system.md`) | Commanding the physical walker (see "Game integration") |

## What the engine is missing

1. **A fixed-step simulation loop.**
   - `Game::start` advances every system by the wall-clock `dt`.
   - Training needs fixed physics ticks (60–480 Hz), a decision period, and running as fast as
     possible, decoupled from frames.
2. **A headless training mode.**
   - `Renderer` (SDL window + Vulkan) is always constructed.
   - Training processes need no window, no renderer and no input/UI systems, and they run N arenas
     each.
3. **3D articulated bodies.**
   - `Position` is a 2D tile position plus height, and `MapManager` owns it.
   - Ragdoll segments need full 3D poses, velocities and joints. They get their own components; at most
     the map places the agent.
4. **Physics.** A `PhysicsSys` that owns the Jolt world and maps bodies to entities.
5. **The agent runtime.** The observation, action, reward, episode, randomisation, stats and exact
   energy systems (the Unity plan, §3.3–3.8).
6. **The ML-Agents bridge** (protocol 1.5.0).
   - The training executable stands in for `TrainingEnv.exe`.
   - **Agent ids must stay stable.** EnTT recycles entity ids, so keep a separate id.
7. **Asset input.** The engine has no skeletal, mesh or animation pipeline. v1 imports the body file
   and the pose tracks exported by Unity. glTF import of meshes and animations can replace the Unity
   bake later.
8. **A 3D body render path.** Debug capsules and boxes first, then a skinned mesh. Plus replay and
   offscreen video capture.
9. **ONNX Runtime inference** for the eval mode and for agents in the game world.
10. **A demo-recorder mode** that writes `.demo` files from pose tracks.

## Engine-specific decisions

| # | Question | Recommendation |
|---|---|---|
| E1 | Training mode of the game executable, or a separate target | **Separate targets on a shared simulation library.** One target is `dynamical` (game); the other is a headless train target that links no Vulkan or SDL. CMake currently globs everything into one executable |
| E2 | Frame convention: Unity is y-up; this map is z-up (`Position::getVec3` = x, y, height) | **Train in Unity's numbers**, which keeps the observation golden tests possible, and convert at the boundary to the map. Swapping y and z is a reflection, which rigid-body physics does not care about |
| E3 | Training arena ground: Jolt boxes or the engine's terrain | **Boxes for v1** (flat and tilted, as in Unity). Use the FastNoise2 terrain as a heightfield once the walker exists |
| E4 | Game time runs ~600× real time (`TimeSys`), physics must run at 1× | **Real-time focus mode**: physical walkers only at a new real-time game speed and near the camera; kinematic `PatherSys` otherwise. See `physical-agents/game-integration.md` |
| E5 | How arenas share the registry and threads | **One `entt::registry` + `SystemSet` + Jolt `PhysicsSystem` per arena**, each stepped single-threaded; Taskflow runs arenas in parallel. EnTT 3.8 registries are not thread-safe for the creates/destroys that resets do |

The Unity plan's decisions still stand:
- **D1:** Unity becomes a data source only; the VR games stay on PhysX.
- **D2:** gRPC.
- **D3:** keep the observation layout.
- **D4:** body choice.
- **D6:** gravity −9.81.

D5 is settled by this engine: EnTT, Taskflow, Windows and NixOS.

## Milestones, adapted

| # | Work in this engine | Gate |
|---|---|---|
| M0 | Bridge skeleton plus a null environment in the headless target | Trainer steps/s at 6 and 20 envs. If it stays near ~424, the trainer is the bottleneck |
| M1 | Fixed-step loop, headless target, `PhysicsSys`, body and joint components, capsule debug draw | A ragdoll loaded from the Unity body file stands in its rest pose and is drawn |
| M2 | Joint limits, motors, contacts, floor | Limit envelopes and drive responses match Unity's contract. The Jolt energy checks pass |
| M3 | Agent runtime in parity mode | Observations, rewards and terminations match Unity on identical poses |
| M4 | Full bridge; `start-run` launches the train target | An unmodified config trains, stats reach W&B, and `stop-run` exports the ONNX |
| M5 | Demo-recorder mode | `.demo` files from Unity pose tracks load in GAIL |
| M6 | First GAIL walker from scratch | It matches the frozen Unity reference on behavioural stats at matched updates |
| M7 | Exact energy channel, DeVita stats, slope | The energetics line reruns on the new simulation |
| M8 | In-engine replay viewer, video capture, ONNX eval mode | Videos and evals with no Unity build |
| M9 | Game integration (below), terrain, then the strike and duel lines | Each gated like M3 + M6 |

## Game integration (M9)

The engine's `human` entities can become physical walkers.

**Layering.**
- The behaviour and LLM layer stays the **planner** (`llm-ai-system.md`: "planner, not controller").
- A commanded walker policy, taking direction, speed and facing as in the Unity `CommandedLowerAgent`
  line, is the **controller**. Jolt does the rest.
- `PatherSys` paths become a stream of walker commands, not tile moves.

**Constraints.**
- **Cost:** a ragdoll per human costs physics and ONNX inference at 30 Hz. Cap the number of physical
  agents, or fall back to kinematic movement beyond the camera.
- **Bodies:** a policy is bound to the body and simulation it trained on. Game humans must use the
  trained body and the same physics settings.
