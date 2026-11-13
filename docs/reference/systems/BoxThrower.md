# BoxThrower

`BoxThrowerSys` in [`src/view/systems/base_scene.cpp`](../../../src/view/systems/base_scene.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: no

Left click throws a 1 m box from the desktop camera: the base scene's way to poke the physics.

## Behaviour

- **tick**: when `Input::leftClick` and ImGui does not want the mouse: `spawnBox` 3 m ahead of the camera along
  `forward()`, size 1, initial velocity forward x 25 m/s, colour from a counter (function static, shared by every
  instance).
- preinit, init, finish: nothing.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Input](../components/Input.md) | context | read | tick | leftClick (UI clears it while ImGui wants the mouse) |
| [Camera](../components/Camera.md) | context | read | tick | position and forward(): the box starts 3 m ahead |
| [ModelManager](../components/ModelManager.md) | context | readwrite | tick (spawnBox) | load("./resources/models/box.gltf"): cached after the first call |
| [ModelC](../components/ModelC.md) | component | write+create | tick (spawnBox, ModelManager::load) | the box model's entity, made the first time the file is loaded |
| [Transform](../components/Transform.md) | component | write+create | tick (spawnBox) | a new box entity at the position, scale = size |
| [ModelRenderable](../components/ModelRenderable.md) | component | write+create | tick (spawnBox) | draws box.gltf tinted with the colour |
| [RigidBody](../components/RigidBody.md) | component | write+create | tick (spawnBox) | dynamic box, half_extent = size / 2, initial_velocity; RigidBodySys makes the Jolt body |

## Side effects

- A new entity with a dynamic Jolt body per click, never destroyed; colours cycle from a function-static counter.

## Order and dependencies

After Input and UI (UI clears `leftClick` over the menus), after whatever places the camera this frame; before
RigidBody / Physics so the box is in this frame's step.

## Used in scenes

<!-- generated -->

## Tests

- `T2.4 base system list ...` (stub). GPU: `P1 check: scripted input flies the camera, throws a box and toggles the grass`.
