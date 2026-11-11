# `src/view/systems/`

> [Source](../../../src/view/systems/) · Parent: [`src/view/`](src-view.md)

## Summary
The original interactive host systems of dynamical, all registered by `registerViewSystems()` as Host systems that need the renderer and listed by the scenes' `host.pre` lane (`common/desktop.json` brings Input, UI, PlayerControl, DebugMenu). They poll SDL input, feed ImGui, fly the desktop camera, drive the VR play area and controller hands, show the debug menu, and create the `base` scene's demo content (box ring, model lineup, box throwing). Most are declared with the `DEFINE_SYSTEM` macro in `system_list.h` (constructor taking the registry, preinit / init / tick / finish, `name()` = class name); InputSys and UISys have their own headers.

## Files
| File | Contents |
|---|---|
| `system_list.h` | `DEFINE_SYSTEM` macro; declares TimeSys, PlayerControlSys, VRControlSys, BoxRingSys, ModelLineupSys, BoxThrowerSys, DebugMenuSys. |
| `input.h` / `input.cpp` | `InputSys`: emplaces `Input`, SDL event polling, scancode -> `Input::Action` map (`actionMap`). |
| `ui.h` / `ui.cpp` | `UISys`: Input -> ImGuiIO, `ImGui::NewFrame`, click masking. |
| `time.cpp` | `TimeSys`: `Time` clock, pause key, speed and frame-time windows. |
| `player_control.cpp` | `PlayerControlSys`: emplaces `Camera`, free-fly camera, R moves the VR rig. |
| `vr_control.cpp` | `VRControlSys` and the file-local `VRHands` context var: stick locomotion, turning, trigger box throwing, kinematic hands. |
| `debug_menu.cpp` | `DebugMenuSys`: ImGui "Dynamical" window. |
| `base_scene.cpp` | `BoxRingSys`, `ModelLineupSys`, `BoxThrowerSys`: base scene content. |

## Systems defined here
- [InputSys](../systems/InputSys.md)
- [UISys](../systems/UISys.md)
- [TimeSys](../systems/TimeSys.md)
- [PlayerControlSys](../systems/PlayerControlSys.md)
- [VRControlSys](../systems/VRControlSys.md)
- [DebugMenuSys](../systems/DebugMenuSys.md)
- [BoxRingSys](../systems/BoxRingSys.md)
- [ModelLineupSys](../systems/ModelLineupSys.md)
- [BoxThrowerSys](../systems/BoxThrowerSys.md)

## Components defined here
- [VRHands](../components/VRHands.md) (file-local context var in `vr_control.cpp`)

## Subfolders
None.
