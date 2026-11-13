# FixedCamera

`FixedCameraSys` in [`src/view/fixed_camera.cpp`](../../../src/view/fixed_camera.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: yes

Puts the desktop camera once, at init, at a given position looking at a given point: a fixed view of the worlds of a
scene (`test_boxes_view`). PlayerControl can still fly it afterwards.

## Behaviour

- **Constructor**: reads `pos` and `look_at` (each an array of 3 numbers) when present.
- **init**: `Camera::position = pos`; with dir = normalize(look_at - pos), `pitch = asin(dir.y)`,
  `yaw = atan2(-dir.x, -dir.z)` (the engine's yaw: 0 looks down -Z).
- **tick**: nothing.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `pos` | [x, y, z] | [0, 10, 20] | camera position |
| `look_at` | [x, y, z] | [0, 0, 0] | point it looks at (must differ from `pos`) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Camera](../components/Camera.md) | context | write | init | position, pitch = asin(dir.y), yaw = atan2(-dir.x, -dir.z) |

## Side effects

- None besides the components and context above.

## Order and dependencies

After PlayerControl in the lane: both set the camera in init, the later one wins (`SystemSet::init` follows the lane
order). A capture shot with a camera overrides it.

## Used in scenes

<!-- generated -->
- [`test_boxes_view.scene.json`](../../../resources/scenes/test_boxes_view.scene.json): `host.pre`

## Tests

- GPU: `V2.1 test_boxes_view at frame 300 matches its golden`, `P2 check: scripted clicks pause and single-step
  WorldStepper` (scene `test_boxes_view`; their shots move the camera themselves).
