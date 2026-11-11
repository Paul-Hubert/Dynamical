# `src/view/`

> [Source](../../../src/view/) · Parent: [`src/`](src.md)

## Summary
The interactive front end of the `dynamical` executable (built with `src/main.cpp`, `src/renderer/` and `src/extra/`, not part of the `dynamical_sim` library). It holds the `Game` class that owns the host registry and runs the frame loop, the capture-script runner, the registration of every host system that needs the renderer (`registerViewSystems`), and the host systems that connect the scene's per-world registries to the screen: `WorldStepperSys` steps the worlds in real time, `WorldViewSys` mirrors their bodies into host entities the renderer draws, `SphereViewSys` and `RollerPlayerSys` view and drive the sphere-roller agents, `FixedCameraSys` places the camera. The original demo systems (input, UI, free camera, VR controls, debug menu, base scene content) live in `systems/`, their data types in `components/`.

## Files
| File | Contents |
|---|---|
| `game.h` / `game.cpp` | `Game`: host registry, Renderer and SystemSet owner; emplaces Settings, tf::Executor, RunArgs, FrameClock, Worlds; scene loading, preinit/init, frame loop, teardown. |
| `register_view.h` / `register_view.cpp` | `registerViewSystems()`: registers Input, UI, Time, PlayerControl, VRControl, DebugMenu, BoxRing, ModelLineup, BoxThrower, Terrain (renderer's `TerrainSys`), WorldStepper, FixedCamera, SphereView, VRRoller, DesktopRoller, WorldView (all Host, needs renderer) and the `ModelRenderable` scene component. |
| `capture.h` / `capture.cpp` | `Capture`: capture-script parser, scripted SDL input, per-shot camera, terrain-ready wait, PNG writer (stb_image_write implementation lives here). |
| `world_stepper.h` / `world_stepper.cpp` | `WorldStepperState` (host context var) and `WorldStepperSys`: real-time fixed-step stepping of `Worlds`, pause / step panel. |
| `world_view.h` / `world_view.cpp` | `WorldViewLink` component and `WorldViewSys`: host mirror entities for world grounds and bodies; static `gridOffset`. |
| `sphere_view.h` / `sphere_view.cpp` | `SphereViewSys`: sphere-roller follow camera, velocity bars, rotation dots, panel. |
| `roller_player.h` / `roller_player.cpp` | `RollerPlayerSys` (`VRRoller` / `DesktopRoller`): player input to `steerSphere`, play area / camera riding the sphere. |
| `fixed_camera.h` / `fixed_camera.cpp` | `FixedCameraSys`: places the desktop camera at init from `pos` / `look_at`. |
| `spawn.h` / `spawn.cpp` | `spawnBox(reg, position, size, color, velocity)`: host entity with Transform, ModelRenderable (`resources/models/box.gltf`) and a Dynamic box RigidBody; returns `entt::null` if the model is missing. Used by BoxRing, BoxThrower, DebugMenu, VRControl. |

## Systems defined here
- [WorldStepperSys](../systems/WorldStepperSys.md)
- [WorldViewSys](../systems/WorldViewSys.md)
- [SphereViewSys](../systems/SphereViewSys.md)
- [RollerPlayerSys](../systems/RollerPlayerSys.md)
- [FixedCameraSys](../systems/FixedCameraSys.md)
- [Game](../systems/Game.md) (service)
- [Capture](../systems/Capture.md) (service)

## Components defined here
- [WorldViewLink](../components/WorldViewLink.md)
- [WorldStepperState](../components/WorldStepperState.md)
- [tf::Executor](../components/tf-Executor.md) (third-party type, emplaced by `Game`)

## Subfolders
- [`src/view/components/`](src-view-components.md)
- [`src/view/systems/`](src-view-systems.md)
