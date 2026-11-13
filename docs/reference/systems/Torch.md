# Torch

`TorchSys` in [`src/logic/sky/sky.cpp`](../../../src/logic/sky/sky.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after the camera's systems (`CameraWalk`) · Parameters: yes

A point light of the scene: an entity with a [Transform](../components/Transform.md) and a
[PointLight](../components/PointLight.md), either carried in front of the desktop camera (a torch in the hand, at
night) or left at a position. The renderer's `Lights` picks the few point lights nearest the viewers each frame
([lighting](../../lighting.md)).

## Behaviour

**init**: a PointLight with `color`, `intensity`, `radius` (defaults of the component: (1, 0.62, 0.3), 12, 18 m),
an entity with a Transform at `position` and that light.

**tick**: when carried and the [Camera](../components/Camera.md) context exists, the torch is put at the camera's
position + `offset.x` along its right, `offset.y` along its up (right x forward), `offset.z` along its forward.

**finish**: destroys the entity.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `color` | [r, g, b] | [1, 0.62, 0.3] | colour of the light |
| `intensity` | float | 12 | light on a surface facing it one metre away |
| `radius` | float | 18 | m: nothing is lit beyond |
| `carried` | bool | true | follows the desktop camera; false: stays at `position` |
| `offset` | [x, y, z] | [0.35, -0.25, 0.8] | from the camera: right, up, ahead |
| `position` | [x, y, z] | [0, 0, 0] | where it starts (and stays when not carried) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Transform](../components/Transform.md) | component | write+create | init | the torch's entity at `position` |
| [PointLight](../components/PointLight.md) | component | write+create | init | colour, intensity and radius from the params |
| [Transform](../components/Transform.md) | component | write | tick | carried: the camera's position + offset along its right, up and forward |
| [Camera](../components/Camera.md) | context | read | tick | position and orientation of the desktop camera |
| [Transform](../components/Transform.md) | component | destroy | finish | the torch's entity is destroyed |
| [PointLight](../components/PointLight.md) | component | destroy | finish | with its entity |

## Side effects

- Creates one entity in init and destroys it in finish.

## Order and dependencies

- After the systems that move the Camera this frame (`CameraWalk`, `PlayerControl`), so the light is where the
  camera is when the frame is drawn.

## Used in scenes

<!-- generated -->
- [`forest_torch.scene.json`](../../../resources/scenes/forest_torch.scene.json): `host.pre`
- [`tautavel_walk_torch.scene.json`](../../../resources/scenes/tautavel_walk_torch.scene.json): `host.pre`

## Tests

- Capture `tests/capture/lighting_torch.capture.json` (scene `forest_torch`).
