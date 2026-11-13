# SphereHeuristic

`SphereHeuristicSys` in [`src/logic/agents/sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, `SphereHeuristic@act` in place of `TrainerActions@act` · Parameters: no

A proportional controller for the sphere roller, to compare with a policy (scene `sphere_view_heuristic`, tests). It
writes the world agent's action from its last observation.

## Behaviour

- **tick**: when the world has an [`AgentIO`](../components/AgentIO.md) context variable whose observation has the
  sphere layout (16 floats), sets `action = sphereHeuristic(observation)`: the torque `up x (v* - v)` in the goal
  frame (target velocity `(0, 0, target_speed)`, velocity's horizontal part), gain 1, each component clamped to
  [-1, 1]. Rolling without slipping, a torque along `up x dv` accelerates the sphere along `dv`.
- Does not create the `AgentIO` (it uses `find`).

## Parameters

None (the gain is fixed at 1).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | context | readwrite | tick | reads the observation, writes the action |

## Side effects

- None.

## Order and dependencies

- In the `@act` slot, before `SphereControl`, which turns the action into the torque of the window. The observation it
  reads is the one `SphereObserve@observe` wrote at the end of the last window.

## Used in scenes

<!-- generated -->
- [`sphere_view_heuristic.scene.json`](../../../resources/scenes/sphere_view_heuristic.scene.json): `fixed@act`

## Tests

- `SR.3`, `SR.6`, `SR.11` in [`tests/unit/sphere_roller.cpp`](../../../tests/unit/sphere_roller.cpp) (through `sphereHeuristic`); `SR.16`, `SR.19`, `SR.20` compare policies against it.
