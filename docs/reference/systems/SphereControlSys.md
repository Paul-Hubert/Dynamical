# SphereControlSys

> **Kind:** system · **Registry name:** `SphereControl` · **Scope:** World · **Lane:** Fixed (Phase Every) · **Needs renderer:** no
> **Source:** [`src/logic/agents/sphere_roller.h`](../../../src/logic/agents/sphere_roller.h), [`src/logic/agents/sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp)

## Purpose
Actuator of the `sphere_roll` agent (one dynamic sphere per world, steered by a torque vector). It creates and configures the world's `AgentIO`, spawns the first episode, and before `PhysicsStep` on every fixed tick applies the torque held for the current decision window. On the first tick of a window it first runs a pending episode reset, then converts `AgentIO::action` (a torque in the goal frame of the observation it answers) into a world torque.

## Lifecycle
- **Construction / params:** `SphereControlSys(reg)`, no params.
- **preinit:** `agentIO(reg)`: emplaces an empty `AgentIO` if the world has none.
- **init:**
  1. Counts the entities of `view<SphereAgent, Transform, RigidBody>`. If the count is not 1, logs `SphereControl: expected one entity with SphereAgent, Transform and RigidBody, found N` at error level; with 0 it returns (AgentIO stays unconfigured, sizes 0).
  2. `agentIO(reg).configure(agent.behavior, SphereObs::size = 16, sphere_actions = 3)` on the first entity of the view.
  3. `resetSphereEpisode(reg)` (see below).
- **tick(h):**
  1. `findAgent`: first entity of `view<SphereAgent, Transform, RigidBody>`; return if none.
  2. If `WorldClock::tick % decision_period == 0` (first tick of the window):
     - if `pending_reset`, `resetSphereEpisode(reg)`;
     - `local = 0`; if `has_decision` and `action.size() >= 3`, `local[i] = isfinite(action[i]) ? clamp(action[i], -1, 1) : 0`;
     - `torque = max_torque * GoalFrame::fromForward(headingDirection(decision_heading)).toWorld(local)`.
  3. If the body is created and `torque != 0`, `PhysicsWorld::bodies().AddTorque(body, torque)`.
- **finish:** None.

`resetSphereEpisode(reg)` (free function, also called by SphereObserveSys and view code):
1. Returns if there is no agent entity. Gets `SphereAgent`, `Transform`, `RigidBody`, `WorldRng` (`get`), `physicsWorld(reg)`.
2. Spawn point: `r = spawn_radius * sqrt(u)`, `angle = uniform(0, 2 pi)`, centre x = `WorldInfo::index * spawn_spacing` (0 without `WorldInfo`), `p = (centre + r cos, 0, r sin)`.
3. If `std::shared_ptr<TerrainColliders>` is in the context: `surface = surfaceHeight(p.x, p.z)`, `spread = overhangAmplitude() + 8`; `require()` the box `[p - (2, *, 2), p + (2, *, 2)]` over `[surface - spread, surface + spread]` at time `WorldClock::tick * dt`; ray from `surface + spread` down `2 * spread`. Otherwise ray from y = 1000 down 2000.
4. `p.y = (hit ? hit.y : 0) + half_extent.x + 0.002`; `home = p`; Transform position `p`, rotation identity; if the body exists, `SetPositionRotationAndVelocity(p, identity, 0, 0)`; if a `PhysicsPose` exists, both poses set to it.
5. Target: steered agents keep `heading = goal_heading` and set `speed = goal_speed = 0`; others draw `heading = uniform(-pi, pi)`, `speed = drawSpeed` (0 with `stop_chance`, else `uniform(speed_range)`), goal = current, `goal_timer = uniform(goal_interval)`.
6. Clears the episode state (`decision_target = 0`, `decision_heading = heading`, `has_decision = false`, `torque = 0`, sums, `reward_ticks`, `decisions`, `ended`, `out_of_bounds`, `pending_reset`), sets `Parked::value = false`, zeroes `AgentIO::action`, clears `reward`, `done`, `truncated`, `stats`.

## Expected behavior
- The torque is constant over a decision window and changes only on ticks where `tick % decision_period == 0`.
- Before the first observation of an episode (`has_decision == false`) the torque is zero, so the first window of every episode is unactuated.
- Non-finite action components become 0; others are clamped to [-1, 1]; the world torque magnitude per axis is at most `max_torque` (N m) in the goal frame (x = up x z, y = up, z = target direction of the observation).
- An action vector shorter than 3 is ignored (zero torque).
- A reset puts the sphere at rest on the first STATIC surface below the spawn point (`half_extent.x + 2 mm` above it), within `spawn_radius` of the world's spawn centre.
- While the world is `Parked` this system does not run (Phase Every), so a parked sphere receives no torque.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | world | write (emplace_ctx) | preinit | `agentIO()` find-or-emplace |
| [SphereAgent](../components/SphereAgent.md) | world | readwrite | init | count, `behavior`; reset writes episode state |
| [Transform](../components/Transform.md) | world | readwrite | init | view; reset writes position / rotation |
| [RigidBody](../components/RigidBody.md) | world | read | init | view; `half_extent.x`, `body` (not yet created) |
| [AgentIO](../components/AgentIO.md) | world | readwrite | init | `configure(behavior, 16, 3)`; reset clears outputs |
| [WorldRng](../components/WorldRng.md) | world | readwrite | init | reset draws |
| [WorldInfo](../components/WorldInfo.md) | world | read | init | `find`; `index` |
| [std::shared_ptr&lt;TerrainColliders&gt;](../components/std--shared_ptr-TerrainColliders.md) | world | readwrite | init | `find`; `surfaceHeight`, `overhangAmplitude`, `require` |
| [WorldClock](../components/WorldClock.md) | world | read | init | `tick * dt` for `require` |
| [PhysicsWorld](../components/PhysicsWorld.md) | world | readwrite | init | `groundBelow`; `SetPositionRotationAndVelocity` if created |
| [PhysicsPose](../components/PhysicsPose.md) | world | write | init | `try_get`; set to the spawn pose |
| [Parked](../components/Parked.md) | world | write | init | `find`; `false` |
| [SphereAgent](../components/SphereAgent.md) | world | readwrite | tick | `pending_reset`, `has_decision`, `decision_heading`, `max_torque`; writes `torque`; reset |
| [RigidBody](../components/RigidBody.md) | world | read | tick | `body`, reset reads `half_extent.x` |
| [WorldClock](../components/WorldClock.md) | world | read | tick | `tick`, `decision_period` |
| [AgentIO](../components/AgentIO.md) | world | readwrite | tick | reads `action`; reset clears it |
| [PhysicsWorld](../components/PhysicsWorld.md) | world | readwrite | tick | `AddTorque`; reset raycast and teleport |
| [Transform](../components/Transform.md) | world | readwrite | tick | reset on pending reset |
| [WorldRng](../components/WorldRng.md) | world | readwrite | tick | reset on pending reset |
| [WorldInfo](../components/WorldInfo.md) | world | read | tick | reset on pending reset |
| [std::shared_ptr&lt;TerrainColliders&gt;](../components/std--shared_ptr-TerrainColliders.md) | world | readwrite | tick | reset on pending reset |
| [PhysicsPose](../components/PhysicsPose.md) | world | write | tick | reset on pending reset |
| [Parked](../components/Parked.md) | world | write | tick | reset on pending reset |

## Side effects
- Jolt: `AddTorque` on the sphere's body every unparked tick with a non-zero torque; on reset, `SetPositionRotationAndVelocity` (teleport, zero velocities) and a `groundBelow` ray cast.
- On reset with marching cubes terrain, `TerrainColliders::require` builds the collider cells around the spawn column synchronously.
- Consumes `WorldRng` draws on every reset (2 for the spawn, then 3 or 4 for the target when not steered: heading, stop chance, speed unless stopped, goal timer).
- Error log in init when the agent entity count is not 1.
- `physicsWorld(reg)` emplaces a default `PhysicsWorld` if the world has none (never the case for scene worlds).

## Ordering and dependencies
- In `sphere_world`: `RigidBody`, `WorldTerrain`, `TrainerActions@act` (or `Policy@act` / `SphereHeuristic@act`), `SphereControl`, `PhysicsStep`, `SphereTask`, `SphereObserve@observe`. The Act system must run before it so that `AgentIO::action` is the current decision; `PhysicsStep` must follow so the torque acts this tick.
- `WorldTerrain` preinit must precede this init for the spawn to see the terrain colliders. At init the Jolt body does not exist yet (RigidBodySys creates it on its first tick, from the Transform written by the reset).
- Single-threaded within its world; several worlds may tick in parallel on the executor (each has its own registry).

## Related
- [SphereTaskSys](SphereTaskSys.md), [SphereObserveSys](SphereObserveSys.md), [TrainerActionsSys](TrainerActionsSys.md), [SphereHeuristicSys](SphereHeuristicSys.md), [PolicySys](PolicySys.md), [PhysicsStepSys](PhysicsStepSys.md), [RigidBodySys](RigidBodySys.md), [WorldTerrainSys](WorldTerrainSys.md)
- [SphereAgent](../components/SphereAgent.md), [AgentIO](../components/AgentIO.md), [Parked](../components/Parked.md)
- [`docs/physical-agents/sphere-roller.md`](../../physical-agents/sphere-roller.md), [`docs/physical-agents/simulation.md`](../../physical-agents/simulation.md)
