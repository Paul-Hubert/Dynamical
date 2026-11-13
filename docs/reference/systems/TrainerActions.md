# TrainerActions

`TrainerActionsSys` in [`src/logic/agents/sphere_roller.cpp`](../../../src/logic/agents/sphere_roller.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, `TrainerActions@act` (first tick of a decision window) · Parameters: no

The action slot of the training templates. During training the host (`TrainingEnv`, the ML-Agents bridge) writes
every agent's `AgentIO::action` (and `discrete`) itself between two windows, so this system does nothing. It exists
so that viewer and evaluation scenes can swap the slot for a system that does write actions, by name:
`"systems": { "replace": { "TrainerActions@act": "Policy@act" } }` (or `SphereHeuristic@act`).

## Behaviour

- **tick**: empty.

## Parameters

None.

## Components and context

None: the system touches nothing.

## Side effects

- None.

## Order and dependencies

- Placed where the actions must be ready: before the systems that read `AgentIO::action` on the first tick of a window
  (`SphereControl`, `AnimalDrive@act`, `Pursuit`, the walker's drive).

## Used in scenes
<!-- generated -->

## Tests

- Indirectly every training scenario on these templates: `SR.9` in [`tests/protocol/sphere_env.cpp`](../../../tests/protocol/sphere_env.cpp), `MA.1` to `MA.5` in [`tests/protocol/multi_env.cpp`](../../../tests/protocol/multi_env.cpp).
