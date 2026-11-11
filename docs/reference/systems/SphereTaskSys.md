# SphereTaskSys

> **Kind:** system · **Registry name:** `SphereTask` · **Scope:** World · **Lane:** Fixed (Phase Every) · **Needs renderer:** no
> **Source:** [`src/logic/agents/sphere_roller.h`](../../../src/logic/agents/sphere_roller.h), [`src/logic/agents/sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp)

## Purpose
Task logic of the `sphere_roll` agent, run after `PhysicsStep` on every fixed tick: it scores the sphere's horizontal velocity against the target velocity the policy observed (`decision_target`), lets the live target heading and speed drift toward their goal at bounded rates (drawing new goals on a timer unless a player steers), and ends the episode, parking the world, when the sphere leaves the bounds or falls.

## Lifecycle
- **Construction / params:** `SphereTaskSys(reg)`, no params.
- **preinit:** None.
- **init:** None.
- **tick(h):**
  1. `findAgent`; return if none. Return if the body is not created.
  2. `p = Transform::position`, `v = GetLinearVelocity(body)`.
  3. If `has_decision`: `reward_sum += sphereReward(v, decision_target, reward_scale)` (`exp(-|v_xz - target_xz| / max(scale, 1e-6))`), `error_sum += |v_xz - target_xz|`, `reward_ticks++`.
  4. If not `steered`: `goal_timer -= h`; when `<= 0`, `drawGoal`: if `home_radius > 0` and the horizontal distance from `home` exceeds it, `goal_heading = atan2(-dz, -dx) + uniform(-pi/4, pi/4)` (back home), else `heading + uniform(-max_goal_turn, max_goal_turn)`; `goal_speed = drawSpeed()`; `goal_timer = uniform(goal_interval)`.
  5. `heading = wrapPi(heading + clamp(wrapPi(goal_heading - heading), -turn_rate h, turn_rate h))`; `speed += clamp(goal_speed - speed, -acceleration h, acceleration h)`.
  6. If (`bounds > 0` and `|p.x| > bounds` or `|p.z| > bounds`) or `p.y < home.y - fall_depth`: `ended = true`, `out_of_bounds = true`, `Parked::value = true`.
- **finish:** None.

## Expected behavior
- Per-tick reward is in (0, 1], 1 when the horizontal velocity equals the decision target; the vertical velocity is ignored.
- No reward is accumulated before the first observation of an episode or after it ended (`has_decision` false).
- The live target heading turns by at most `turn_rate * h` rad and its speed changes by at most `acceleration * h` per tick; heading stays in [-pi, pi).
- Steered agents never draw goals (the goal comes from `steerSphere`), but still turn and accelerate toward it and still end on bounds / fall.
- Leaving the bounds or falling parks the world: the remaining Every / Act systems of the window (including this one and PhysicsStep) stop until `SphereObserve` ends the episode.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [SphereAgent](../components/SphereAgent.md) | world | readwrite | tick | window sums, goal state, heading / speed, `ended`, `out_of_bounds` |
| [RigidBody](../components/RigidBody.md) | world | read | tick | `body` |
| [Transform](../components/Transform.md) | world | read | tick | `position` |
| [PhysicsWorld](../components/PhysicsWorld.md) | world | read | tick | `GetLinearVelocity` |
| [WorldRng](../components/WorldRng.md) | world | readwrite | tick | `drawGoal` (3 or 4 draws) when the goal timer expires |
| [Parked](../components/Parked.md) | world | write | tick | `find`; `true` on out of bounds / fall |

## Side effects
- Consumes `WorldRng` draws on each goal draw (non-steered agents).
- No Jolt writes; reads the body's linear velocity.

## Ordering and dependencies
- After `PhysicsStep` (scores the velocity produced by this tick's step) and before `SphereObserve@observe`, which turns the sums into `AgentIO::reward`.
- Reads `Transform::position`, which `PhysicsStepSys` updates for the world's dynamic bodies.

## Related
- [SphereControlSys](SphereControlSys.md), [SphereObserveSys](SphereObserveSys.md), [PhysicsStepSys](PhysicsStepSys.md)
- [SphereAgent](../components/SphereAgent.md), [Parked](../components/Parked.md)
- [`docs/physical-agents/sphere-roller.md`](../../physical-agents/sphere-roller.md)
