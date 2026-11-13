# GroundPlane

`GroundPlaneSys` in [`src/logic/agents/ground_field.cpp`](../../../src/logic/agents/ground_field.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list (only its preinit does anything) · Parameters: yes

Makes a tilted plane the world's [`GroundField`](../components/GroundField.md) (docs/physical-agents/ik-gait.md): the
density surface the kinematic walk plants its feet on, and the ground of the animals' demonstrations, with no
collider involved. Scenes on the marching cubes terrain replace it with [GroundTerrain](GroundTerrain.md).

## Behaviour

- **constructor**: reads `pitch` and `roll` (degrees).
- **preinit**: `insert_or_assign` the context variable `std::shared_ptr<GroundField>` with a new `PlaneField(pitch,
  roll)` through the origin. The plane is tilted by `pitch` about +x then `roll` about +z: positive pitch rises toward
  -z (uphill for a walker heading 0), positive roll rises toward +x. Any field set before is replaced.
- **tick**: nothing.
- Without this system (or `GroundTerrain`), `groundField(reg)` makes a level plane at y = 0 on first use.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `pitch` | float | 0 | degrees about +x; positive rises toward -z |
| `roll` | float | 0 | degrees about +z; positive rises toward +x |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [GroundField](../components/GroundField.md) | context | write+create | preinit | the plane, as `std::shared_ptr<GroundField>` |

## Side effects

- None (the plane is analytic: no bodies, no files).

## Order and dependencies

- Its preinit must come before the init of the systems that copy the field's pointer (`AgentFactory` keeps it in
  `HumanBot::ground`; `AnimalFactory`, herds, gait scenes look it up in init): any place in the fixed list does, since
  every preinit runs before any init.
- A later `PlaneField::setTilt` (tests) bumps the field's `version`, and `GaitFootIK` fits the planted prints again.

## Used in scenes
<!-- generated -->

## Tests

- `IK.7`, `GAIT.1` to `GAIT.5`, `GAIT.7` to `GAIT.13` in [`tests/unit/ik_gait.cpp`](../../../tests/unit/ik_gait.cpp) (scene `ik_world`, plane tilts)
- `TW.1`, `TW.2`, `TW.4`, `TW.9` in [`tests/unit/terrain_walker.cpp`](../../../tests/unit/terrain_walker.cpp)
