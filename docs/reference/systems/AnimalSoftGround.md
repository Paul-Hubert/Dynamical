# AnimalSoftGround

`AnimalSoftGroundSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after `AnimalDrive@act`, before `PhysicsStep` (only in `common/animal_world.json`) · Parameters: no

Soft ground (mud, snow) for the physical animal: the hard ground lies `soft_depth` under the surface, and a body whose
lowest point is inside that layer is held by a drag on its velocity, full sideways, a part of it upwards and none
downwards, so that a foot sinks to the hard ground and pulling it straight out is the cheap way.

## Behaviour

- **tick**: returns when the bot is not built, is kinematic, or the episode's `soft_depth` is 0 (drawn by
  `resetAnimalBot` from the ranges' `soft` probability, or fixed by the spawn's `soft_depth`). For every segment:
  - `depth = soft_depth - (lowest point of its shapes - ground under it)`; nothing when `depth <= 0`;
  - mass = the body's mass x the segment's `percentage` / 100;
  - `softGroundForce(params, mass, walk_speed, depth / soft_depth, linear velocity, h)`:
    `k = soft_resistance x m g x inside / max(walk_speed, 0.05)`, force = `(-k vx, vy > 0 ? -k soft_lift vy : 0, -k vz)`,
    each axis capped at what stops the body within the step (`m / h x |v|`);
  - `AddForce` on the segment's Jolt body.
  `soft_resistance` (3) and `soft_lift` (0.25) are the params file's, overridable by the env parameters
  `softResistance` and `softLift`. The ground under a point is `AnimalBot::groundUnder` (the ground field, or y = 0).

## Parameters

None (see `AnimalParams::soft_resistance`, `soft_lift`, and `AnimalSpawn::soft_depth`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalBot](../components/AnimalBot.md) | context | read | tick | segment states, soft depth, params, body, ground field |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | tick | `AddForce` on the segments' bodies |

## Side effects

- Adds forces to the ragdoll's Jolt bodies before the step.

## Order and dependencies

- After `AnimalSync` of the previous tick (it reads the segment states and velocities that system stored), before
  `PhysicsStep` (the forces apply to the next step).
- The orders and terrain world templates do not list it: there an episode's soft depth, when the ranges draw one,
  only shows in the observation.

## Used in scenes
<!-- generated -->

## Tests

- `AA.9` (the soft depth is in the observation, the animal stands in it, `softGroundForce` drags more sideways than upwards and nothing downwards, capped by the step), in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp).
