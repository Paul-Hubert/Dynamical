# `src/logic/agents/`

> [Source](../../../src/logic/agents/) · Parent: [`src/logic/`](src-logic.md)

## Summary
Learning agents that live in world registries. `AgentIO` is the per-world contract with the training host (WorldsEnvironment / TrainHost, PolicySys, tests): the host writes `action` and `reset_requested`, the world's Observe phase writes observation, reward, done, truncated and stats. `sphere_roller.*` implements the `sphere_roll` task (`docs/physical-agents/sphere-roller.md`): a dynamic sphere steered by a 3D torque toward a target velocity that drifts over the episode, with the `SphereAgent` component, the goal frame helpers, the reward, the reset, and five world systems split by decision-window phase. `registerSphereRoller()` (called from `registerSimSystems`) registers the `SphereAgent` scene component and the systems. `steerSphere`, `leanSpeed` and `resetSphereEpisode` are also used by the player-steered viewer systems (RollerPlayer: VRRoller / DesktopRoller).

Fixed-lane order in `sphere_world`: `RigidBody`, `WorldTerrain`, `TrainerActions@act`, `SphereControl`, `PhysicsStep`, `SphereTask`, `SphereObserve@observe` (`sphere_world_hills` drops WorldTerrain; viewers replace `TrainerActions@act` with `Policy@act` or `SphereHeuristic@act`).

## Files
| File | Contents |
|---|---|
| [`agent_io.h`](../../../src/logic/agents/agent_io.h) | `AgentIO` (behaviour, sizes, sensor name, action / reset in, observation / reward / done / truncated / stats out, `configure`), `agentIO(reg)` find-or-emplace helper. |
| [`sphere_roller.h`](../../../src/logic/agents/sphere_roller.h) | `GoalFrame` (`fromForward`, `toLocal`, `toWorld`), `headingDirection`, `SphereAgent`, `SphereObs` layout (16 floats), `sphere_actions = 3`, `sphereReward`, `steerSphere`, `leanSpeed`, `sphereHeuristic`, `resetSphereEpisode`, the five system classes (TrainerActionsSys inline no-op), `registerSphereRoller`. |
| [`sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp) | Helpers (`wrapPi`, `findAgent`, `drawSpeed`, `drawGoal`, `groundNormal`, `park`), the free functions, the system bodies, and `registerSphereRoller` (SphereAgent JSON parse with validation and serializer; five `registerSystem` calls, all `SystemScope::World`). |

Free functions (not systems):
- `GoalFrame::fromForward(f)`: y = up, z = horizontal unit of f (+X when f is vertical), x = y x z.
- `sphereReward(v, target, scale) = exp(-|v_xz - target_xz| / max(scale, 1e-6))`.
- `steerSphere(agent, v)`: `goal_speed = |v_xz|`; `goal_heading = atan2(v.z, v.x)` only when that speed > 1e-4.
- `leanSpeed(lean, deadzone, range, top)`: 0 up to deadzone, `top` from range on, `top * log(lean / deadzone) / log(range / deadzone)` between.
- `sphereHeuristic(obs, gain = 1)`: `clamp(gain * up x ((0, 0, obs[15]) - (obs[9], 0, obs[11])), -1, 1)`.
- `resetSphereEpisode(reg)`: respawn at rest on the ground near the world's spawn centre, fresh target, cleared episode state and AgentIO outputs, unpark (details in [SphereControlSys](../systems/SphereControlSys.md)).

## Systems defined here
- [SphereControlSys](../systems/SphereControlSys.md)
- [SphereTaskSys](../systems/SphereTaskSys.md)
- [SphereObserveSys](../systems/SphereObserveSys.md)
- [TrainerActionsSys](../systems/TrainerActionsSys.md)
- [SphereHeuristicSys](../systems/SphereHeuristicSys.md)

## Components defined here
- [AgentIO](../components/AgentIO.md)
- [SphereAgent](../components/SphereAgent.md)

## Subfolders
None.
