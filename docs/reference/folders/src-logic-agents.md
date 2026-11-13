# src/logic/agents

Physical and kinematic agents of the training worlds (docs/physical-agents/): the HumanBot walker on Jolt (a ragdoll
driven by motors, the agent of `walker_*` scenes), its assets, energy books and energy formulas; the kinematic walk
(procedural gait, IK, rig poses) that records demonstrations for it; the ground fields and terrain routes it walks
on; the sphere roller and pursuit fixtures; and what every agent of the engine shares: the `AgentIO` exchange with
the training host, the environment parameters and the ragdoll build (also used by `src/logic/animals/` and
`src/logic/sandbox/`). Everything here runs in a world's registry (`fixed` lane), not in the host's.

## Files

| File | What |
|---|---|
| [`agent_io.h`](../../../src/logic/agents/agent_io.h) | `AgentIO` (an agent as the host sees it: behaviour, actions, observations, sensors, reward, done, stats), `AgentSensor`, `AgentStat`, `AgentsReset`, `agentIO(reg)` |
| [`agent_env_params.h`](../../../src/logic/agents/agent_env_params.h) | `AgentEnvParams`: the trainer's environment parameters, a world context variable written by the host |
| [`ragdoll.h`](../../../src/logic/agents/ragdoll.h), [`ragdoll.cpp`](../../../src/logic/agents/ragdoll.cpp) | `RagdollBuild`, `RagdollParts`, `buildRagdollParts`, `destroyRagdollParts`, `ragdollInertia`: Jolt bodies and swing-twist motor constraints of a `BodyDef` (walker and animals) |
| [`humanbot_assets.h`](../../../src/logic/agents/humanbot_assets.h), [`humanbot_assets.cpp`](../../../src/logic/agents/humanbot_assets.cpp) | the Unity exports: `BodyDef` (segments, joints, shapes), `PoseTrack`, `AgentParams`; `loadBody`, `parseBody`, `loadTrack`, `loadParams`, `parseParams`, `fileSha256`, Unity-to-engine frame conversion |
| [`humanbot.h`](../../../src/logic/agents/humanbot.h) | the walker: `HumanBot` (context), `HumanBotSpawn`, `WorldShape`, `PoseTrackPlayer`, the books (`EnergyBooks`, `DeVitaBooks`, `FormulaBooks`, `StanceWindow`), `WalkLeg`, `HeadingFrame`, `observeV2`, `resetHumanBot`, `applySlope`, the system classes |
| [`humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) | systems AgentFactory, DriveTargets, Disturbances, BodySync, Task, Termination, Rewards, Observation, Episode, TrackActions; episode reset, env params, observations; component registration of `HumanBotSpawn`, `PoseTrackPlayer` |
| [`humanbot_energy.cpp`](../../../src/logic/agents/humanbot_energy.cpp) | systems Energy and DeVitaStepStat, `applySlope`, registration of the energy systems |
| [`humanbot_formulas.cpp`](../../../src/logic/agents/humanbot_formulas.cpp) | system EnergyFormulas (Lagrangian, recursive Newton-Euler, motor work, checks), `ContactRecorder`, `FormulaTotals::operator+=` |
| [`ground_field.h`](../../../src/logic/agents/ground_field.h), [`ground_field.cpp`](../../../src/logic/agents/ground_field.cpp) | `GroundField` (plane or marching cubes surface as a density, no collider), `PlaneField`, `TerrainField`, `groundField(reg)`; systems GroundPlane, GroundTerrain |
| [`walk_routes.h`](../../../src/logic/agents/walk_routes.h), [`walk_routes.cpp`](../../../src/logic/agents/walk_routes.cpp) | `WalkRoutes` (slope band of a walker's episodes), `measureRoute`, `drawDirection`, `drawStart`, `GaitRoute`; system GaitRoutes |
| [`rig_pose.h`](../../../src/logic/agents/rig_pose.h), [`rig_pose.cpp`](../../../src/logic/agents/rig_pose.cpp) | `RigPose` (joint angles), `RigModel` (forward kinematics of a body file without Jolt), `JointLimit`, `SwingTwist`, `Rig`, the rig entity |
| [`ik.h`](../../../src/logic/agents/ik.h), [`ik.cpp`](../../../src/logic/agents/ik.cpp) | FABRIK: `IKChain`, `IKTarget`, `solveChain`; systems Fabrik, RigPoseApply |
| [`gait.h`](../../../src/logic/agents/gait.h), [`gait.cpp`](../../../src/logic/agents/gait.cpp) | the procedural gait: `GaitCommand`, `GaitParams`, feet, hands, balance, `GaitReport`; systems GaitBase, GaitFootIK, GaitHandIK, GaitBalance, GaitChecks |
| [`sphere_roller.h`](../../../src/logic/agents/sphere_roller.h), [`sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp) | the sphere roller fixture (`SphereAgent`, `GoalFrame`); systems SphereControl, SphereTask, SphereObserve, TrainerActions, SphereHeuristic |
| [`pursuit.h`](../../../src/logic/agents/pursuit.h), [`pursuit.cpp`](../../../src/logic/agents/pursuit.cpp) | the pursuit fixture (several agents as components): `PursuitParams`, `PursuitDot`, `PursuitField`; systems Pursuit, PursuitObserve |

## Systems

HumanBot walker (fixed lane order of `common/humanbot_world.json`: `RigidBody`, AgentFactory, `TrainerActions@act`,
DriveTargets@act, Disturbances, `PhysicsStep`, BodySync, Energy, EnergyFormulas, Task, Termination, DeVitaStepStat,
Rewards@observe, Observation@observe, Episode@observe):

- [AgentFactory](../systems/AgentFactory.md): the walker, its ragdoll and the first episode
- [DriveTargets](../systems/DriveTargets.md): the action as motor gains and targets (act)
- [Disturbances](../systems/Disturbances.md): random pushes
- [BodySync](../systems/BodySync.md): segment states after the step, drawn shapes
- [Energy](../systems/Energy.md): energy books of the step
- [EnergyFormulas](../systems/EnergyFormulas.md): the three energy formulas (eval)
- [Task](../systems/Task.md): new targets and speeds every `arcFrames`
- [Termination](../systems/Termination.md): kill and truncation
- [DeVitaStepStat](../systems/DeVitaStepStat.md): DeVita's stance work, steps
- [Rewards](../systems/Rewards.md), [Observation](../systems/Observation.md), [Episode](../systems/Episode.md): the observe phase
- [TrackActions](../systems/TrackActions.md): pose tracks on the kinematic rig (`rig_world`)

Kinematic walk and ground: [GroundPlane](../systems/GroundPlane.md), [GroundTerrain](../systems/GroundTerrain.md),
[GaitRoutes](../systems/GaitRoutes.md), [GaitBase](../systems/GaitBase.md), [GaitFootIK](../systems/GaitFootIK.md),
[GaitHandIK](../systems/GaitHandIK.md), [GaitBalance](../systems/GaitBalance.md), [Fabrik](../systems/Fabrik.md),
[RigPoseApply](../systems/RigPoseApply.md), [GaitChecks](../systems/GaitChecks.md).

Fixtures: [SphereControl](../systems/SphereControl.md), [SphereTask](../systems/SphereTask.md),
[SphereObserve](../systems/SphereObserve.md), [TrainerActions](../systems/TrainerActions.md),
[SphereHeuristic](../systems/SphereHeuristic.md), [Pursuit](../systems/Pursuit.md),
[PursuitObserve](../systems/PursuitObserve.md).

## Components and context variables

Walker and shared ones (this page's part):

- [AgentIO](../components/AgentIO.md): the world's agent (context) or an agent entity (component)
- [AgentsReset](../components/AgentsReset.md): context flag, the host's RESET for worlds of component agents
- [AgentEnvParams](../components/AgentEnvParams.md): context, the trainer's environment parameters
- [HumanBot](../components/HumanBot.md): context, the walker's whole state
- [HumanBotSpawn](../components/HumanBotSpawn.md): component, the scene's request for a walker
- [WorldShape](../components/WorldShape.md): component, a drawn shape of a world body (also the animals and the sandbox)
- [PoseTrackPlayer](../components/PoseTrackPlayer.md): component, track playback on the rig
- [ContactRecorderHandle](../components/ContactRecorderHandle.md): context, private to `humanbot_formulas.cpp`

Kinematic walk, ground and fixtures (documented with their systems): [GroundField](../components/GroundField.md),
[WalkRoutes](../components/WalkRoutes.md), [GaitRoute](../components/GaitRoute.md), [RigPose](../components/RigPose.md),
[Rig](../components/Rig.md), [IKChain](../components/IKChain.md), [IKTarget](../components/IKTarget.md),
[GaitCommand](../components/GaitCommand.md), [GaitParams](../components/GaitParams.md),
[SphereAgent](../components/SphereAgent.md), [PursuitDot](../components/PursuitDot.md),
[PursuitField](../components/PursuitField.md).

## Worth knowing

- `agentIO(reg)`: the world's `AgentIO`, created on first use. Agents that are components (sandbox, pursuit) create
  `AgentsReset` in preinit instead.
- `buildRagdollParts` / `destroyRagdollParts` / `ragdollInertia`: the ragdoll build shared with `AnimalBot`
  (`src/logic/animals/animal_agent.cpp`) and the sandbox. Segments never collide with each other (group filter);
  `animal_layer` puts ragdolls on the layer where they collide with each other; `cast` for mesh ground.
- `loadBody`, `loadParams`, `loadTrack`: the Unity exports under `resources/agents/` (bodies, params, tracks), in the
  engine frame, hashed (`file_hash`) for run metadata and the demo cache. `resourceFile` finds `resources/` in the
  working directory or next to the executable.
- `resetHumanBot`: an episode start (env params, speed, spawn, target, slope, randomisation, books).
- `observeV2`, `headingFrame`: the `heidelbot_notoes_2` observation layout (Unity's), used by tests as an oracle.
- `HumanBot::currentHeight`, `groundUnder`: heights measured from the ground field when there is one.
- `StanceWindow<W>`: a debounced stance window of one foot (DeVita's protocol, the formulas).
- Readers outside the folder: `src/train/train_host.cpp` (eval summary, replays, cost of transport tables, legs CSV
  from `HumanBot`), `src/train/worlds_env.cpp` (gathers `AgentIO` agents), `src/onnx/` (policies write
  `AgentIO::action`), `src/view/walker_view.cpp`, `src/view/world_view.cpp` (draws `WorldShape`).

## Dependencies

- `src/physics/` (Jolt `PhysicsWorld`, `PhysicsLayers`, `PhysicsPose`, `TerrainColliders`, `TerrainUsers`),
  `src/scene/` (system and component registries, `World`, `WorldClock`, `WorldRng`), `src/logic/systems/` (`System`,
  `Parked`), `src/logic/components/` (`Transform`, `ModelRef`, `Ground`, `RigidBody`), `src/logic/terrain/`
  (`TerrainGenerator`, `MarchingCubes` for `TerrainField`), `src/util/` (log, paths).
- Used by `src/logic/animals/`, `src/logic/sandbox/`, `src/train/`, `src/onnx/`, `src/view/`.

## Design docs

- [docs/physical-agents/agent-runtime.md](../../physical-agents/agent-runtime.md), [physics.md](../../physical-agents/physics.md),
  [assets.md](../../physical-agents/assets.md), [simulation.md](../../physical-agents/simulation.md),
  [energy-formulas.md](../../physical-agents/energy-formulas.md), [rig-animation.md](../../physical-agents/rig-animation.md),
  [ik-gait.md](../../physical-agents/ik-gait.md), [sphere-roller.md](../../physical-agents/sphere-roller.md),
  [mlagents-bridge.md](../../physical-agents/mlagents-bridge.md),
  [reports/terrain-walker.md](../../physical-agents/reports/terrain-walker.md)
- [docs/action-agents.md](../../action-agents.md) (several agents in a world, `AgentIO` as a component)
