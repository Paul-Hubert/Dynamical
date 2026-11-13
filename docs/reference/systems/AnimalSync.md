# AnimalSync

`AnimalSyncSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, right after `PhysicsStep` · Parameters: no

Reads the state of the ragdoll after each physics step into the `AnimalBot` (positions, rotations, velocities of every
segment, finite differences of their origins) and counts the tick; with `draw`, moves the shape entities that
`AnimalFactory` made.

## Behaviour

- **tick**: returns when the bot is not built. `syncAnimalBot(reg, bot, h)`:
  - `readAnimalState`: position, rotation, linear and angular velocity of every segment's Jolt body;
  - `origin_velocity[i] = (position - origin_previous) / h` when the history is valid and `h > 0`, else zero (after a
    teleport of the kinematic walk, or the observe-only tick of an eval); `origin_previous = position`,
    `history_valid = true`;
  - `bot.tick++`, `bot.window_ticks++`.
- With `bot.draw`: for every entity with `WorldShape` and `Transform`, the pose of its segment times its local pose is
  written to the `Transform` and to its `PhysicsPose` (created on first use, the previous pose shifted into
  `prev_position` / `prev_rotation`), which `WorldView` interpolates.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | tick (syncAnimalBot) | segment states, origin velocities, `tick`, `window_ticks` |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | read | tick (readAnimalState) | bodies' positions, rotations, velocities |
| [WorldShape](../components/WorldShape.md) | component | read | tick (draw) | segment and local pose of a drawn shape |
| [Transform](../components/Transform.md) | component | write | tick (draw) | the shape's world pose |
| [PhysicsPose](../components/PhysicsPose.md) | component | readwrite+create | tick (draw) | last two poses of the shape |

## Side effects

- None beyond the registry (reads Jolt).

## Order and dependencies

- Right after `PhysicsStep`; before `AnimalTask`, `AnimalTermination`, `AnimalChecks` and the observe phase systems,
  which all read the state it stores.
- `origin_velocity` is what the observation, the rewards and the checks call a velocity.

## Used in scenes

<!-- generated -->
- [`common/animal_demo_terrain_world.json`](../../../resources/scenes/common/animal_demo_terrain_world.json): `fixed`
- [`common/animal_demo_world.json`](../../../resources/scenes/common/animal_demo_world.json): `fixed`
- [`common/animal_orders_world.json`](../../../resources/scenes/common/animal_orders_world.json): `fixed`
- [`common/animal_terrain_world.json`](../../../resources/scenes/common/animal_terrain_world.json): `fixed`
- [`common/animal_world.json`](../../../resources/scenes/common/animal_world.json): `fixed`

## Tests

- Every `AA.*` test that steps a world (`AA.2` to `AA.9`, `AA.11`) in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp).
