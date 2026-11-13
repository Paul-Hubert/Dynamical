# ModelLineup

`ModelLineupSys` in [`src/view/systems/base_scene.cpp`](../../../src/view/systems/base_scene.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre (base) · Parameters: no

Part of the base scene: at init, every glTF file dropped into `resources/models` (not its subfolders, not
`box.gltf`) is loaded and lined up behind the spawn point, scaled to about 4 m tall, as a dynamic body with a box
collider around its bounds.

## Behaviour

- **init**: iterates `./resources/models` (errors ignored, directory order); for each `.gltf` / `.glb`:
  `ModelManager::load` (skipped if it fails), scale = 4 / height of the `ModelC` bounds, position (x, ground -
  min.y x scale + 2, 15) with x from -20 by 8 m; `Transform`, `ModelRenderable` (no tint), dynamic `RigidBody` with
  `half_extent` = half the scaled bounds and `offset` = the scaled bounds' centre.
- preinit, tick, finish: nothing.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [ModelManager](../components/ModelManager.md) | context | readwrite | init | load() of each file found |
| [ModelC](../components/ModelC.md) | component | read+create | init | made by ModelManager::load; its min / max give the scale, the collider and the height |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | surfaceHeight() under each model; required |
| [Transform](../components/Transform.md) | component | write+create | init | x from -20 every 8 m, z = 15, 2 m above the ground, scale to 4 m tall |
| [ModelRenderable](../components/ModelRenderable.md) | component | write+create | init | the loaded model, untinted |
| [RigidBody](../components/RigidBody.md) | component | write+create | init | dynamic box around the model's bounds (half_extent, offset) |

## Side effects

- Lists ./resources/models with std::filesystem (errors ignored) and loads each .gltf / .glb: files read, GPU buffers uploaded by the model loader.
- One dynamic Jolt body per model (RigidBodySys).

## Order and dependencies

Needs `TerrainGenerator` (ChunkGenerator) and `ModelManager` (renderer). Before RigidBody / Physics.

## Used in scenes

<!-- generated -->

## Tests

- `T2.4 base system list ...` (stub); base captures and probes (`V0.x`, `V1.1`, `V2.2`, `S0.1`, `S1.1`).
