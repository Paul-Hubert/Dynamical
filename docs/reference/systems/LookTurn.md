# LookTurn

`LookTurnSys` in [`src/logic/player/player_body.cpp`](../../../src/logic/player/player_body.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after the input systems · Parameters: no

Unity's LookTurn: the view axis of the player's input turns its `PlayerView`. A mouse gives radians turned this frame (`viewIsDelta()`), a stick a deflection turned at `stick_speed` rad/s.

## Behaviour

- **tick**: for every entity with `LookTurn` and `PlayerView`, `input = playerInput(reg, entity)`;
  `turn = viewAxis` (delta input) or `viewAxis x stick_speed x dt`; `yaw -= turn.x`;
  `pitch = clamp(pitch + turn.y, -1.55, 1.55)`.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [ScriptedInput](../components/ScriptedInput.md) | component | read | helper playerInput | answers for the player when present (tests, scripts) |
| [MouseKeyboardInput](../components/MouseKeyboardInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [ControllersInput](../components/ControllersInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [VRPlayerInput](../components/VRPlayerInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [LookTurn](../components/LookTurn.md) | component | read | tick | `stick_speed`, and the filter of the players turned |
| [PlayerView](../components/PlayerView.md) | component | readwrite | tick | yaw decreased by the axis' x, pitch increased by its y, clamped to +-1.55 rad |

## Side effects

- None.

## Order and dependencies

After the device systems (`MouseKeyboardInput`, `ControllersInput`, viewer) so that it sees this frame's input; before `Mounting` / `MountSteering`, `PlayerLegs` and `Hold`, which use the view's yaw, and `PlayerViewpoint`.

## Used in scenes

<!-- generated -->

## Tests

`PLAYER.2` (turning the view turns the walk) and `PLAYER.8` in [`tests/unit/player.cpp`](../../../tests/unit/player.cpp); `MOUNT.3` in [`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp).
