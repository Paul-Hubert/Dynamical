# Input

`InputSys` in [`src/view/systems/input.cpp`](../../../src/view/systems/input.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre (first) · Parameters: no

Turns the SDL event queue of the desktop window into the [Input](../components/Input.md) context variable: held
actions by key, mouse buttons, clicks, relative motion, wheel, typed text, focus and window state. Every desktop scene
has it first (`common/desktop.json`), and everything that reads the keyboard or the mouse reads `Input`, not SDL.

## Behaviour

- **Constructor**: emplaces `Input` (so it exists before any system's `preinit`; `Game::start` keeps a reference to it).
- **tick**:
  1. Clears the per-frame fields: `leftClick`, `rightClick`, `middleClick`, `mouseWheel`, `mouseDelta`. The held
     bits of `on` and the `*Down` / `mouse*` buttons are kept from frame to frame.
  2. Drains `SDL_PollEvent`:
     - window resized: sets `Input::RESIZE` (never cleared here);
     - focus gained / lost: `focused`; restored / minimised: `window_showing`;
     - `SDL_QUIT` or Escape pressed: sets `Input::EXIT`;
     - key down / up: sets / clears the action mapped to the scancode (physical key) — W `FORWARD`, S `BACKWARD`,
       A `LEFT`, D `RIGHT`, P `PAUSE`, Space `UP`, left Ctrl `DOWN`, left Shift `SPRINT`, R `VR_RIG`, E `INTERACT`,
       left Alt `CURSOR`, M `MENU`, K `DEBUG`, Backspace `BACKSPACE`, Tab `TAB`, arrows `ROTATE_LEFT` / `ROTATE_RIGHT` /
       `ANGLE_UP` / `ANGLE_DOWN`;
     - mouse buttons: `mouseLeft` / `leftDown` (and right, middle) follow the button; a press sets the `*Click` flag;
     - motion: `mouseDelta += (xrel, yrel)`; wheel: `mouseWheel` = the last event's (x, y);
     - text input: `text` = the event's UTF-8 text (UI consumes it).
  3. `screenSize` from `SDL_GetWindowSize(ctx.win)`; while focused, `mousePos` from `SDL_GetMouseState`.

Some bits are one-shot flags that their consumer clears: `PAUSE` (Time), `VR_RIG` (PlayerControl), `CURSOR`
(MouseKeyboardInput), `EXIT` (`Game::start`).

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Input](../components/Input.md) | context | readwrite+create | constructor (emplace), tick | clears the per-frame clicks, wheel and motion, then sets the action bits, buttons, motion, text, focus, screen size and mouse position |
| [Context](../components/Context.md) | context | read | tick | the SDL window (Context::win) for its size; stored as Context* |

## Side effects

- Drains SDL's event queue every frame (SDL_PollEvent): no other system may poll it, and the capture's scripted events pushed by Capture::beginFrame are read here.
- Reads the window size (SDL_GetWindowSize) and, while focused, the cursor position (SDL_GetMouseState).
- Escape or closing the window sets Input::EXIT, which ends the main loop of Game::start.

## Order and dependencies

- Needs the renderer's `Context*` context variable (the SDL window): the renderer is made before the systems.
- First of the host pre lane: UI and every input reader come after it. A capture's scripted input is pushed into
  SDL's queue by `Capture::beginFrame` before the systems tick, so this system sees it as real input the same frame.

## Used in scenes

<!-- generated -->

## Tests

- `T2.4 base system list after the PhysicsSys split` (`tests/unit/base_scene.cpp`): its place in the base scene (stub).
- GPU capture tests (CMake, label `gpu`): `P1 check: scripted input flies the camera, throws a box and toggles the grass`
  (`tests/capture/base_interactive.capture.json`), `P2 check: scripted clicks pause and single-step WorldStepper`
  (`tests/capture/worlds_stepper.capture.json`).
