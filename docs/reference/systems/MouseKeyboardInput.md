# MouseKeyboardInput

`MouseKeyboardInputSys` in [`src/view/player_input.cpp`](../../../src/view/player_input.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: no

The viewer's side of the physical players (docs/players.md): the keyboard and the mouse of the window go to the
[MouseKeyboardInput](../components/MouseKeyboardInput.md) component of the [DesktopView](../components/DesktopView.md)
player, which says what they mean (`GenericPlayerInput`). The mouse is captured (relative mode); left Alt frees it for
the menus and takes it back.

## Behaviour

- **tick**:
  1. `Input::CURSOR` (left Alt): clears it, toggles `captured` (starts true).
  2. `SDL_SetRelativeMouseMode(captured)`; ImGui's `NoMouse` flag while captured (the menus do not get its clicks).
  3. For each entity with `DesktopView` and `MouseKeyboardInput`: `keys` reset, then W, S, D, A, Space, LeftShift, E
     from the `Input` actions of those physical keys (none while ImGui wants the keyboard); `captured`; `mouse` =
     `mouseDelta`; `left` / `right` = the raw buttons (`mouseLeft` / `mouseRight`, not the UI-filtered ones); `wheel` =
     `mouseWheel.y`; `last_action` = the system's time when a key is down or, captured, the mouse moved, a button is
     down or the wheel turned (the device used last speaks for the player, `playerInput`).
- **finish**: clears ImGui's `NoMouse`, relative mouse mode off.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Input](../components/Input.md) | context | readwrite | tick | the action bits, mouseDelta, mouseLeft / mouseRight, wheel; clears Input::CURSOR once used |
| [DesktopView](../components/DesktopView.md) | tag | read | tick | the player whose devices these are |
| [MouseKeyboardInput](../components/MouseKeyboardInput.md) | component | write | tick | keys (W A S D Space LeftShift E, none while ImGui wants the keyboard), captured, mouse, left, right, wheel, last_action |

## Side effects

- SDL_SetRelativeMouseMode on while captured (every tick); ImGui's NoMouse config flag set while captured; both undone in finish.

## Order and dependencies

After Input and UI; after PlayerSpawn (which makes the player) and before the player systems that read the device
(LookTurn, PlayerLegs, Hold, Mounting, ...). PlayerControl does nothing while a `DesktopView` player exists.

## Used in scenes

<!-- generated -->

## Tests

- `MOUNT.1 the input components answer for their devices ...` (`tests/unit/mount.cpp`) tests the component it fills
  (without the system). GPU captures `tests/capture/player_fps.capture.json`, `player_horse.capture.json`.
