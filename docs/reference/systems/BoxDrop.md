# BoxDrop

`BoxDropSys` in [`src/logic/systems/box_drop.cpp`](../../../src/logic/systems/box_drop.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list (before `RigidBody`, which makes the bodies on its first tick) · Parameters: yes

Test-scene world system: in init, drops `count` dynamic bodies on a centred grid above the ground, with the world's
RNG for the jitter, heights, sizes and orientations; every `capsule_every`-th one is a capsule. Nothing happens on
ticks. The physics regression scenes (`test_boxes`, `test_boxes_view`) are built on it.

## Behaviour

- **constructor**: reads the parameters.
- **init**: for `i` in `0..count-1`, on a grid of `cols = ceil(sqrt(count))` columns and `ceil(count / cols)` rows,
  centred on the origin, `spacing` apart:
  - `half` uniform in `half_extent`; position `x, z` at the grid cell plus a jitter uniform in ±0.2 × `spacing`,
    `y` uniform in `height` plus `half`; rotation a uniform angle around a uniform axis (`WorldRng::unitSphere`).
    Draws come from `WorldRng` in that order, so the drop depends only on the world's seed;
  - a new entity with `Transform` and a dynamic `RigidBody` (friction 1, restitution 0);
  - a capsule when `capsule_every > 0` and `i % capsule_every == capsule_every - 1`: radius `0.6 half`, half height
    `1.5 half`, angular damping 1 (Jolt has no rolling resistance), `ModelRef("models/primitives/capsule", orange)`;
  - else a cube of half extent `half` and `ModelRef("models/primitives/box.gltf")` of a random blue.
- **tick**: nothing.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `count` | int | 50 | bodies dropped |
| `spacing` | float | 1.4 | metres between grid cells |
| `height` | [float, float] | [0.5, 2] | range of the drop height: the centre is at a draw in it plus the half size |
| `half_extent` | [float, float] | [0.15, 0.3] | range of the half size of a body |
| `capsule_every` | int | 0 | every n-th body is a capsule, 0: none |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [WorldRng](../components/WorldRng.md) | context | readwrite | init | jitter, height, size, orientation, colour draws (`get`: must exist, a world's) |
| [Transform](../components/Transform.md) | component | write+create | init | pose of each dropped body |
| [RigidBody](../components/RigidBody.md) | component | write+create | init | dynamic box or capsule |
| [ModelRef](../components/ModelRef.md) | component | write+create | init | the primitive model and colour `WorldView` draws |

## Side effects

- Creates `count` entities in init.

## Order and dependencies

- Needs `WorldRng` (worlds only). Its bodies become Jolt bodies on the first tick of [RigidBody](RigidBody.md),
  which lands them on the template's `Ground` (made in RigidBody's init).
- The order of the draws is the determinism contract of `S2.2` / `S2.3`: changing it changes every test_boxes result.

## Used in scenes
<!-- generated -->

## Tests

- `S2.1`, `S2.2`, `S2.3` ([`tests/unit/test_boxes.cpp`](../../../tests/unit/test_boxes.cpp)).
- `V2.1 test_boxes_view at frame 300 matches its golden`, `P2 check: scripted clicks pause and single-step WorldStepper` ([`CMakeLists.txt`](../../../CMakeLists.txt), [`tests/capture/worlds_boxes.capture.json`](../../../tests/capture/worlds_boxes.capture.json)).
- `test_boxes` is also the world of protocol tests in [`tests/protocol/multi_env.cpp`](../../../tests/protocol/multi_env.cpp) and [`tests/protocol/sphere_env.cpp`](../../../tests/protocol/sphere_env.cpp).
