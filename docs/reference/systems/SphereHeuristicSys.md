# SphereHeuristicSys

> **Kind:** system · **Registry name:** `SphereHeuristic` · **Scope:** World · **Lane:** Fixed (Phase Act) · **Needs renderer:** no
> **Source:** [`src/logic/agents/sphere_roller.h`](../../../src/logic/agents/sphere_roller.h), [`src/logic/agents/sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp)

## Purpose
A hand-written action source for comparison with a trained policy (`sphere_view_heuristic` replaces `TrainerActions@act` with `SphereHeuristic@act`): on the first tick of each window it computes `sphereHeuristic(observation)` from the last observation and writes it as the action.

## Lifecycle
- **Construction / params:** `SphereHeuristicSys(reg)`, no params.
- **preinit:** None.
- **init:** None.
- **tick(h):** `io = ctx().find<AgentIO>()`; if present and `observation.size() == 16`, `io->action = sphereHeuristic(io->observation)` with gain 1: `torque = up x ((0, 0, target_speed) - (v.x, 0, v.z))` in the goal frame (rolling without slipping), each component clamped to [-1, 1].
- **finish:** None.

## Expected behavior
- `action` has 3 components in [-1, 1]; when the observed horizontal velocity equals the target, the action is zero. With `v = 0` and target speed s, the action is `(min(s, 1), 0, 0)` (torque about the goal-frame x axis rolls the sphere along +z).
- No effect when there is no `AgentIO` or the observation size is not 16 (e.g. an unconfigured world).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | world | readwrite | tick | `find`; reads `observation`, writes `action` |

## Side effects
None.

## Ordering and dependencies
- Act phase, before `SphereControl`, which converts the action into a torque on the same tick.
- Reads the observation written by the previous window's `SphereObserve`.

## Related
- [TrainerActionsSys](TrainerActionsSys.md), [PolicySys](PolicySys.md), [SphereControlSys](SphereControlSys.md), [SphereObserveSys](SphereObserveSys.md)
- [AgentIO](../components/AgentIO.md)
