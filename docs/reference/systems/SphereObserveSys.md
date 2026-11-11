# SphereObserveSys

> **Kind:** system · **Registry name:** `SphereObserve` · **Scope:** World · **Lane:** Fixed (Phase Observe) · **Needs renderer:** no
> **Source:** [`src/logic/agents/sphere_roller.h`](../../../src/logic/agents/sphere_roller.h), [`src/logic/agents/sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp)

## Purpose
Sensor and episode bookkeeping of the `sphere_roll` agent, run on the last tick of each decision window (and alone by `SystemSet::fixed_observe_only` for a trainer RESET). It either performs a host-requested reset or publishes the window's mean reward, stats and end-of-episode flags into `AgentIO`; then it writes the 16-float observation of the live target and makes that target the decision target of the next window.

## Lifecycle
- **Construction / params:** `SphereObserveSys(reg)`, no params.
- **preinit:** None.
- **init:** None.
- **tick(h):** `h` unused (0 when called by `fixed_observe_only`).
  1. `findAgent`; return if none. `io = agentIO(reg)`.
  2. If `io.reset_requested`: clear it, `resetSphereEpisode(reg)` (see [SphereControlSys](SphereControlSys.md)), and if `stagger`, `decisions = int(uniform() * max_decisions)`.
  3. Else if not `pending_reset`:
     - `io.reward = reward_ticks > 0 ? reward_sum / reward_ticks : 0`; `io.stats.clear()`; if `reward_ticks > 0`, stat `Sphere/Speed error` = `error_sum / reward_ticks`.
     - `decisions++`; if not `steered` and `decisions >= max_decisions`, `ended = true`.
     - `io.done = io.truncated = ended` (every end is a limit).
     - If `ended`: stat `Sphere/Out of bounds` = 1 or 0, `pending_reset = true`, `Parked::value = false`.
     - Clear `reward_sum`, `error_sum`, `reward_ticks`.
     (When `pending_reset` is already set and no RESET is requested, the reward block is skipped and `AgentIO` keeps its previous outputs; this happens only if Observe runs twice without SphereControl in between.)
  4. Observation (always): `v`, `w` = body linear / angular velocity (0 if not created); `frame = GoalFrame::fromForward(headingDirection(heading))`; `along` = horizontal velocity direction if its speed > 0.05 m/s else `frame.z`; `above = half_extent.x + 2`.
     - `[0..2]` ground normal below `p`, `[3..5]` ground normal at `p + along * probe_distance`, `[6..8]` ground normal at `p + frame.z * probe_distance` (each `groundBelow` from `above` over the point down `above + 50`, zero vector when there is no hit), all in the goal frame;
     - `[9..11]` `v` and `[12..14]` `w` in the goal frame; `[15]` `speed` (target speed, m/s).
  5. `decision_target = targetVelocity()`, `decision_heading = heading`, `has_decision = !ended`.
- **finish:** None.

## Expected behavior
- `io.observation` has 16 floats after every Observe; normals are unit vectors in the goal frame or zero off the field. In the goal frame, the target velocity is always `(0, 0, observation[15])`.
- `io.reward` is the mean per-tick reward of the window just ended, in [0, 1]; 0 when no tick was scored (first window of an episode).
- `done` and `truncated` are always equal; they are true exactly on the window where the time limit (`max_decisions`, unsteered only) or `SphereTask`'s bounds / fall check ended the episode. That terminal observation still describes the old episode; the reset happens on the first tick of the next window (SphereControl), whose window applies zero torque.
- On RESET the reward / done / stats are cleared by the reset and the observation is of the fresh spawn; with `stagger`, the decision counter starts at a random point so worlds do not all end together.
- Runs even while the world is Parked (Observe systems ignore Parked), and unparks it at the end of an episode.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | world | readwrite | tick | `reset_requested`; writes `reward`, `stats`, `done`, `truncated`, `observation` |
| [SphereAgent](../components/SphereAgent.md) | world | readwrite | tick | sums, `decisions`, `ended`, `pending_reset`, decision target; reset on RESET |
| [WorldRng](../components/WorldRng.md) | world | readwrite | tick | RESET: reset draws + 1 stagger draw |
| [Parked](../components/Parked.md) | world | write | tick | `false` at episode end and on reset |
| [RigidBody](../components/RigidBody.md) | world | read | tick | `body`, `half_extent.x` |
| [Transform](../components/Transform.md) | world | readwrite | tick | `position`; reset writes it on RESET |
| [PhysicsWorld](../components/PhysicsWorld.md) | world | readwrite | tick | velocities, 3 `groundBelow` casts; reset teleports on RESET |
| [WorldInfo](../components/WorldInfo.md) | world | read | tick | reset on RESET |
| [std::shared_ptr&lt;TerrainColliders&gt;](../components/shared_ptr-TerrainColliders.md) | world | readwrite | tick | reset on RESET (`require`) |
| [WorldClock](../components/WorldClock.md) | world | read | tick | reset on RESET (with TerrainColliders) |
| [PhysicsPose](../components/PhysicsPose.md) | world | write | tick | reset on RESET |

## Side effects
- Three Jolt ray casts (`groundBelow`) per observation; on RESET, a teleport of the body and possibly synchronous terrain collider building (see SphereControlSys).
- Consumes `WorldRng` draws on RESET.

## Ordering and dependencies
- Last system of the fixed lane (`SphereObserve@observe`), after `SphereTask`, so the window's sums are complete.
- The host (WorldsEnvironment / TrainHost, PolicySys, tests) reads `AgentIO` after this phase and writes the next `action` before the next window's Act phase.
- `groundBelow` is called outside the Jolt step only (no locking), which holds since all fixed systems of a world run sequentially.

## Related
- [SphereControlSys](SphereControlSys.md), [SphereTaskSys](SphereTaskSys.md), [SphereHeuristicSys](SphereHeuristicSys.md), [PolicySys](PolicySys.md), [WorldsEnvironment](WorldsEnvironment.md)
- [AgentIO](../components/AgentIO.md), [SphereAgent](../components/SphereAgent.md), [Parked](../components/Parked.md)
- [`docs/physical-agents/sphere-roller.md`](../../physical-agents/sphere-roller.md), [`docs/physical-agents/simulation.md`](../../physical-agents/simulation.md)
