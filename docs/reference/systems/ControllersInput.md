# ControllersInput

`ControllersInputSys` in [`src/view/player_input.cpp`](../../../src/view/player_input.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: no

The first gamepad SDL finds goes to the [ControllersInput](../components/ControllersInput.md) component of the
[DesktopView](../components/DesktopView.md) player (docs/players.md): sticks, triggers, A, X, the left shoulder, the
d-pad. Not tried with a real gamepad yet.

## Behaviour

- **preinit**: unless the run is deterministic (captures and probes behave the same with or without a gamepad on the
  desk), `SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)`.
- **tick**:
  1. With the subsystem: a detached pad is closed; while none is open, every second, opens the first joystick that is
     a game controller.
  2. For each entity with `DesktopView` and `ControllersInput`: `connected`; with a pad, sticks (y flipped: SDL's are
     positive down), triggers in 0..1, A, X, left shoulder, `dpad` = +1 / -1 on the frame up / down is pressed;
     `last_action` when a stick is out of its dead zone, a button or a trigger (> 0.5) is pressed.
- **finish**: closes the pad, quits the subsystem.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [RunArgs](../components/RunArgs.md) | context | read | preinit | deterministic(): captures and probes do not open the game controller subsystem |
| [DesktopView](../components/DesktopView.md) | tag | read | tick | the player whose devices these are |
| [ControllersInput](../components/ControllersInput.md) | component | readwrite | tick | connected, sticks, triggers, a, x, left_shoulder, dpad (a notch per press), last_action; reads its deadzone |

## Side effects

- SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) in preinit (not in captures and probes), SDL_QuitSubSystem in finish.
- Looks for a gamepad every second while none is open (SDL_GameControllerOpen), closes it when detached and in finish.

## Order and dependencies

After PlayerSpawn, before the player systems (as MouseKeyboardInput).

## Used in scenes

<!-- generated -->
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.pre`
- [`player_fps.scene.json`](../../../resources/scenes/player_fps.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`

## Tests

- `MOUNT.1 ...` (`tests/unit/mount.cpp`) tests the component (without the system).
