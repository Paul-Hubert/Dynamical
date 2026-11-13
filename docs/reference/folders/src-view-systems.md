# src/view/systems

The systems of the original viewer: the window input, ImGui, the game clock, the free desktop camera, simple VR
locomotion, the debug menu, and the three systems of the base scene (master's `SceneSys`, split).

## Files

| File | What |
|---|---|
| [`system_list.h`](../../../src/view/systems/system_list.h) | `DEFINE_SYSTEM`: declares `TimeSys`, `PlayerControlSys`, `VRControlSys`, `BoxRingSys`, `ModelLineupSys`, `BoxThrowerSys`, `DebugMenuSys` |
| [`input.h`](../../../src/view/systems/input.h), [`input.cpp`](../../../src/view/systems/input.cpp) | `InputSys`: SDL events into `Input` (scancode to action map) |
| [`ui.h`](../../../src/view/systems/ui.h), [`ui.cpp`](../../../src/view/systems/ui.cpp) | `UISys`: ImGui platform layer, `ImGui::NewFrame` |
| [`time.cpp`](../../../src/view/systems/time.cpp) | `TimeSys`: the calendar, the "Time" and "Frame time" windows |
| [`player_control.cpp`](../../../src/view/systems/player_control.cpp) | `PlayerControlSys`: makes and flies the desktop camera |
| [`vr_control.cpp`](../../../src/view/systems/vr_control.cpp) | `VRControlSys` and `VRHands`: stick locomotion, turn, box throwing, kinematic hands |
| [`debug_menu.cpp`](../../../src/view/systems/debug_menu.cpp) | `DebugMenuSys`: the "Dynamical" window |
| [`base_scene.cpp`](../../../src/view/systems/base_scene.cpp) | `BoxRingSys`, `ModelLineupSys`, `BoxThrowerSys` |

## Systems registered from here

Registered in `src/view/register_view.cpp`: [Input](../systems/Input.md), [UI](../systems/UI.md),
[Time](../systems/Time.md), [PlayerControl](../systems/PlayerControl.md), [VRControl](../systems/VRControl.md),
[DebugMenu](../systems/DebugMenu.md), [BoxRing](../systems/BoxRing.md), [ModelLineup](../systems/ModelLineup.md),
[BoxThrower](../systems/BoxThrower.md).

## Components and context variables defined here

- [VRHands](../components/VRHands.md) (context, anonymous namespace of `vr_control.cpp`).

## Worth knowing

- Input actions are bound to physical keys (scancodes), so WASD stay in place on other layouts.
- One-shot `Input` flags are cleared by their consumer: `PAUSE` (Time), `VR_RIG` (PlayerControl; DebugMenu also sets
  it), `CURSOR` (MouseKeyboardInput).
- PlayerControl steps aside when a physical player (`DesktopView`) exists.

## Dependencies

`src/view/components`, `src/view/spawn.h`, `src/logic/components` (Camera, VR, RigidBody, Transform, RenderSettings,
lighting), `src/logic/settings`, `src/logic/sky`, `src/logic/terrain`, `src/logic/animals`, `src/logic/vegetation`,
`src/logic/player`, `src/renderer` (`Context`, `ModelManager`, `ModelC`, `TerrainSys` chunk tags), SDL2, ImGui.

## Design docs

CLAUDE.md (Game loop, VR), docs/physical-agents/scenes.md (base scene split), docs/graphics.md (the Graphics combo),
docs/players.md (PlayerControl and the physical players).
