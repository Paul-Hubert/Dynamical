# Disturbances

`DisturbancesSys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, before `PhysicsStep` · Parameters: no

Random pushes on the HumanBot walker during training (Unity's `pushing`): now and then an impulse and an angular
impulse on one segment.

## Behaviour

**tick** (every tick, not while parked). Does nothing unless the bot is built, dynamic, randomising
(`params.randomize` and not recording) and `params.pushing`. With probability `push_chance` per tick (`WorldRng`):
picks a segment uniformly, `strength = push_force x total_mass`, a force `unitSphere x strength x cbrt(u)` with its
y made positive and a torque drawn the same way, and applies them with `AddImpulse` / `AddAngularImpulse` (an
impulse of that many N s, not a force over the step). `pulling` / `pullChance` / `pullForce` are parsed but not used.

## Parameters

None; `pushing`, `pushChance` (0.001), `pushForce` (5, times the mass) and `randomizing` from the params file or the
environment parameters.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | read | tick | params, bodies |
| [WorldRng](../components/WorldRng.md) | context | readwrite | tick | the chance, the segment, the force and torque draws |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | tick | `BodyInterface::AddImpulse`, `AddAngularImpulse` |

## Side effects

- Impulses on a Jolt body of the walker.

## Order and dependencies

- Before `PhysicsStep`. Its draws come from the world's random stream: it changes the sequence of every later draw
  (targets, speeds) when it is on.

## Used in scenes

<!-- generated -->
- [`common/humanbot_terrain_world.json`](../../../resources/scenes/common/humanbot_terrain_world.json): `fixed`
- [`common/humanbot_world.json`](../../../resources/scenes/common/humanbot_world.json): `fixed`

## Tests

- Covered only through the walker scenes (`tests/unit/humanbot.cpp` S4.3 runs `walker_train`); no test of its own.
