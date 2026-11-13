# PlayerModels

`PlayerModelsSys` in [`src/view/player_input.cpp`](../../../src/view/player_input.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: no

The simulation makes the players' hands and props without a model (it runs without a renderer too); in the viewer
this system gives each of them a box (`resources/models/box.gltf`, a unit cube: the `Transform`'s scale is the box).

## Behaviour

- **tick** (nothing without `ModelManager`): collects the entities with `Hand` (blue for the left side, red for the
  right) or `Prop` (its `color`) and no `ModelRenderable`; when there are any, loads box.gltf (stops when it fails) and
  emplaces `ModelRenderable { box, colour }` on each.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [ModelManager](../components/ModelManager.md) | context | readwrite | tick | load("./resources/models/box.gltf") |
| [ModelC](../components/ModelC.md) | component | write+create | tick (ModelManager::load) | the box model on its first load |
| [Hand](../components/Hand.md) | component | read | tick | side: blue left, red right |
| [Prop](../components/Prop.md) | component | read | tick | color |
| [ModelRenderable](../components/ModelRenderable.md) | component | write+create | tick | added to every Hand and Prop without one (unit box, the Transform's scale is the box) |

## Side effects

- None besides the components and context above.

## Order and dependencies

After the systems that make hands and props (PlayerSpawn and the player systems); any place in the pre lane works,
a late one only delays the model by a frame.

## Used in scenes

<!-- generated -->

## Tests

GPU captures of the player scenes (`tests/capture/player_*.capture.json`).
