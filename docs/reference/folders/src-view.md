# src/view

The viewer (`dynamical`): the game loop with its renderer, the capture runner, and the host systems that only make
sense with a window — cameras, input devices, ImGui panels, and the systems that draw the worlds of a scene. Nothing in
the simulation library (`dynamical_sim`) depends on it.

## Files

| File | What |
|---|---|
| [`game.h`](../../../src/view/game.h), [`game.cpp`](../../../src/view/game.cpp) | `Game`: the registry, its first context variables (`Settings`, `tf::Executor`, `RunArgs`, `Graphics`, `FrameClock`), the scene, the renderer, the `SystemSet`, the `Worlds`, the main loop (prepare, pre tick, post tick, probe, capture, render) |
| [`register_view.h`](../../../src/view/register_view.h), [`register_view.cpp`](../../../src/view/register_view.cpp) | `registerViewSystems()`: registers the 24 view systems and the `ModelRenderable` scene component (needs the renderer's `ModelManager`) |
| [`capture.h`](../../../src/view/capture.h), [`capture.cpp`](../../../src/view/capture.cpp) | `Capture` (capture scripts: input, shot cameras, PNGs) and `terrainReady`, `vegetationReady`, `animalsReady`, `lightingReady` |
| [`spawn.h`](../../../src/view/spawn.h), [`spawn.cpp`](../../../src/view/spawn.cpp) | `spawnBox`: a dynamic box entity (box.gltf, `RigidBody`) |
| [`world_stepper.h`](../../../src/view/world_stepper.h), [`world_stepper.cpp`](../../../src/view/world_stepper.cpp) | `WorldStepperSys` and `WorldStepperState`: steps the worlds in real time |
| [`world_view.h`](../../../src/view/world_view.h), [`world_view.cpp`](../../../src/view/world_view.cpp) | `WorldViewSys` and `WorldViewLink`: host entities mirroring the worlds' bodies; `gridOffset` |
| [`fixed_camera.h`](../../../src/view/fixed_camera.h), [`fixed_camera.cpp`](../../../src/view/fixed_camera.cpp) | `FixedCameraSys`: the camera at a point, looking at another |
| [`camera_walk.h`](../../../src/view/camera_walk.h), [`camera_walk.cpp`](../../../src/view/camera_walk.cpp) | `CameraWalkSys` and `CameraWalk`: the walk through the forests |
| [`sandbox_watch.h`](../../../src/view/sandbox_watch.h), [`sandbox_watch.cpp`](../../../src/view/sandbox_watch.cpp) | `SandboxWatchSys`: the camera round the sandbox's animals |
| [`sphere_view.h`](../../../src/view/sphere_view.h), [`sphere_view.cpp`](../../../src/view/sphere_view.cpp) | `SphereViewSys`: follow camera, bars and panel of the sphere roller |
| [`roller_player.h`](../../../src/view/roller_player.h), [`roller_player.cpp`](../../../src/view/roller_player.cpp) | `RollerPlayerSys`: VRRoller and DesktopRoller, players riding a steered sphere |
| [`walker_view.h`](../../../src/view/walker_view.h), [`walker_view.cpp`](../../../src/view/walker_view.cpp) | `WalkerViewSys`: camera and panel of the physical human walker |
| [`gait_view.h`](../../../src/view/gait_view.h), [`gait_view.cpp`](../../../src/view/gait_view.cpp) | `GaitViewSys`: camera, markers and panel of the kinematic walk |
| [`animal_view.h`](../../../src/view/animal_view.h), [`animal_view.cpp`](../../../src/view/animal_view.cpp) | `AnimalAgentViewSys`: skins, camera, orders and log of the physical animals |
| [`player_input.h`](../../../src/view/player_input.h), [`player_input.cpp`](../../../src/view/player_input.cpp) | `MouseKeyboardInputSys`, `ControllersInputSys`, `PlayerModelsSys`: the devices and models of the physical players |
| [`components/`](src-view-components.md) | `Input`, `Time`, `ModelRenderable` |
| [`systems/`](src-view-systems.md) | the systems of the original viewer: Input, UI, Time, PlayerControl, VRControl, DebugMenu, the base scene |

## Systems registered from here

All in [`register_view.cpp`](../../../src/view/register_view.cpp), host scope, renderer needed:
[Input](../systems/Input.md), [UI](../systems/UI.md), [Time](../systems/Time.md),
[PlayerControl](../systems/PlayerControl.md), [VRControl](../systems/VRControl.md), [DebugMenu](../systems/DebugMenu.md),
[BoxRing](../systems/BoxRing.md), [ModelLineup](../systems/ModelLineup.md), [BoxThrower](../systems/BoxThrower.md),
[Terrain](../systems/Terrain.md) (class in `src/renderer/terrain/`), [WorldStepper](../systems/WorldStepper.md),
[FixedCamera](../systems/FixedCamera.md), [SandboxWatch](../systems/SandboxWatch.md),
[CameraWalk](../systems/CameraWalk.md), [SphereView](../systems/SphereView.md), [VRRoller](../systems/VRRoller.md),
[DesktopRoller](../systems/DesktopRoller.md), [AnimalAgentView](../systems/AnimalAgentView.md),
[WalkerView](../systems/WalkerView.md), [GaitView](../systems/GaitView.md),
[MouseKeyboardInput](../systems/MouseKeyboardInput.md), [ControllersInput](../systems/ControllersInput.md),
[PlayerModels](../systems/PlayerModels.md), [WorldView](../systems/WorldView.md).
Service: [Capture](../systems/service-Capture.md).

## Components and context variables defined here

- [WorldStepperState](../components/WorldStepperState.md) (context, `world_stepper.h`)
- [WorldViewLink](../components/WorldViewLink.md) (component, `world_view.h`)
- [CameraWalk](../components/CameraWalk.md) (context, `camera_walk.h`)
- in `components/`: [Input](../components/Input.md), [Time](../components/Time.md), [ModelRenderable](../components/ModelRenderable.md)
- in `systems/`: [VRHands](../components/VRHands.md)

## Worth knowing

- `Game::Game` / `Game::start`: argument parsing (unknown keys only warn), deterministic runs (captures and probes:
  960 x 540, no headset, graphics defaults unless `graphics=` is given), graphics level applied before the command line's
  own keys, scene loading (a capture script names its scene), renderer before systems, `Worlds` when the scene has
  worlds; order preinit (renderer, systems, worlds), init (systems, worlds, renderer); the loop ends on `Input::EXIT`,
  `ticks=`, or the end of a capture with `exit_after_capture`. The destructor finishes worlds, systems, renderer and
  destroys the systems before the renderer (they own GPU resources).
- `spawnBox(reg, position, size, color, velocity)`: the box every demo throws.
- `WorldViewSys::gridOffset(index, count, spacing)`: where world i is drawn; AnimalAgentView uses it too.
- The `ModelRenderable` scene component (`{ "path", "tint" }`) is registered here because it needs `ModelManager`.

## Dependencies

`src/renderer` (Renderer, `Context`, `ModelManager`, `TerrainSys`), `src/scene` (scenes, `Worlds`, `RunArgs`, probes,
registries), `src/logic/*` (components, settings, terrain, vegetation, animals, agents, player, sandbox),
`src/physics` (`PhysicsPose`, `PhysicsWorld`), `src/onnx` (`PolicySources`, with `DYNAMICAL_ONNX`), `src/util`; SDL2,
ImGui, stb_image_write, taskflow, nlohmann-json.

## Design docs

docs/players.md (input systems, PlayerModels), docs/forest.md, docs/tautavel-walk.md and docs/profiling.md
(CameraWalk), docs/action-agents.md (SandboxWatch), docs/physical-agents/scenes.md and simulation.md (WorldStepper,
WorldView), docs/physical-agents/sphere-roller.md (SphereView, rollers), docs/physical-agents/ik-gait.md (GaitView),
docs/physical-agents/reports/animal-agents.md (AnimalAgentView), docs/physical-agents/reports/terrain-walker.md
(WalkerView's terrain camera), docs/grass.md and docs/physical-agents/phases.md (captures).
