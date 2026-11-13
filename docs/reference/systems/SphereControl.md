# SphereControl

`SphereControlSys` in [`src/logic/agents/sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, before `PhysicsStep` (every tick) · Parameters: no

The actuator of the sphere roller (docs/physical-agents/sphere-roller.md): one dynamic sphere per world, steered by a
single torque vector. It configures the world's agent, starts the first episode, and on every physics tick applies to
the sphere the torque that the last action asked for, expressed in the goal frame of the observation that action
answered.

## Behaviour

- **preinit**: makes sure the world has an [`AgentIO`](../components/AgentIO.md) context variable (`agentIO(reg)`
  emplaces one), which tells the training host the world has an agent before anything else exists.
- **init**: looks for the one entity with [`SphereAgent`](../components/SphereAgent.md) +
  [`Transform`](../components/Transform.md) + [`RigidBody`](../components/RigidBody.md). If there is not exactly one it
  logs an error; with none it returns (the world then has an unconfigured agent). Otherwise it calls
  `AgentIO::configure(behavior, 16, 3)` (16 observation floats, `SphereObs`; 3 continuous actions) and
  `resetSphereEpisode`.
- **tick** (every tick of the fixed lane):
  - On the first tick of a decision window (`WorldClock::tick % decision_period == 0`): if the agent's
    `pending_reset` is set (the terminal observation went out with the last window), it starts a new episode with
    `resetSphereEpisode`. Then it reads `AgentIO::action[0..2]` (non finite components become 0, others clamped to
    [-1, 1]) only when `has_decision` is set, and stores
    `torque = max_torque * GoalFrame(decision_heading).toWorld(action)` on the agent. Without a decision the torque is 0.
  - Every tick: when the Jolt body exists and the held torque is not zero, `BodyInterface::AddTorque` on it.
- **`resetSphereEpisode`** (helper, also called by `SphereObserve`): draws a spawn point within `spawn_radius` of the
  world's spawn centre (`WorldInfo::index * spawn_spacing`, 0); when the world has
  [`TerrainColliders`](../components/TerrainColliders.md) it first builds the colliders of a 4 m column around the
  point (from the generator's surface height ± overhang amplitude + 8 m) with `require`; casts down to the ground
  (`PhysicsWorld::groundBelow`), puts the sphere on it at rest (`Transform`, Jolt position, rotation and velocities,
  `PhysicsPose` when present), sets `home`. A steered agent keeps the heading it was last steered to and speed 0; any
  other draws a random heading and a speed (`speed_range`, or 0 with `stop_chance`), which holds for one goal
  interval. Clears the decision, held torque, reward and error sums, counters and end flags, un-parks the world
  (`Parked::value = false`), and clears the `AgentIO` action, reward, done, truncated and stats.

## Parameters

None for the system. The task settings are the fields of the [`SphereAgent`](../components/SphereAgent.md) component.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [SphereAgent](../components/SphereAgent.md) | component | readwrite | init, tick, resetSphereEpisode | task settings; episode state reset, held torque written from the action |
| [Transform](../components/Transform.md) | component | write | resetSphereEpisode | spawn position, identity rotation |
| [RigidBody](../components/RigidBody.md) | component | read | init, tick, resetSphereEpisode | the Jolt body id, `half_extent.x` as the radius |
| [PhysicsPose](../components/PhysicsPose.md) | component | write | resetSphereEpisode | both interpolation poses set to the spawn (when present) |
| [AgentIO](../components/AgentIO.md) | context | readwrite+create | preinit (create), init (configure), tick (action), resetSphereEpisode (cleared) | the world's one agent |
| [WorldClock](../components/WorldClock.md) | context | read | tick, resetSphereEpisode | window boundaries; simulation time for the colliders |
| [WorldRng](../components/WorldRng.md) | context | readwrite | resetSphereEpisode | spawn point, first target |
| [WorldInfo](../components/WorldInfo.md) | context | read | resetSphereEpisode | world index for `spawn_spacing` (optional) |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | tick, resetSphereEpisode | `AddTorque`, ground ray cast, teleport of the body |
| [TerrainColliders](../components/TerrainColliders.md) | context | readwrite | resetSphereEpisode | `require` of the spawn column (optional, `std::shared_ptr`) |
| [Parked](../components/Parked.md) | context | write | resetSphereEpisode | the world is un-parked at a new episode (when present) |

## Side effects

- Adds a torque to the sphere's Jolt body every tick the held torque is not zero.
- On reset: teleports the Jolt body (position, rotation, zero velocities) and builds the marching cubes colliders around
  the spawn point synchronously (`TerrainColliders::require`, Jolt static mesh bodies).
- Log: an error in init when the world does not hold exactly one sphere agent.

## Order and dependencies

- After `RigidBody` (the Jolt body must exist, otherwise the torque and the teleport are skipped) and `WorldTerrain`
  when the ground is the marching cubes terrain; before `PhysicsStep` so that the torque acts on this tick's step.
- The action slot (`TrainerActions@act`, `Policy@act` or `SphereHeuristic@act`) comes before it in the list, so that
  on the first tick of a window the action is already written.
- `SphereObserve@observe` sets `decision_heading` and `has_decision` that the next window's torque uses, and the
  `pending_reset` this system acts on.

## Used in scenes

<!-- generated -->
- [`common/sphere_world.json`](../../../resources/scenes/common/sphere_world.json): `fixed`
- [`common/sphere_world_hills.json`](../../../resources/scenes/common/sphere_world_hills.json): `fixed`

## Tests

- `SR.4` to `SR.8`, `SR.11`, `SR.12`, `SR.17`, `SR.22`, `SR.23` in [`tests/unit/sphere_roller.cpp`](../../../tests/unit/sphere_roller.cpp)
- `SR.9`, `SR.10` in [`tests/protocol/sphere_env.cpp`](../../../tests/protocol/sphere_env.cpp)
- `SR.16` in [`tests/onnx/policy_runner.cpp`](../../../tests/onnx/policy_runner.cpp); `SR.18` to `SR.20` in [`tests/onnx/vr_roller.cpp`](../../../tests/onnx/vr_roller.cpp)
