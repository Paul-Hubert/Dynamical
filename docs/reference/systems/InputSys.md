# InputSys

> **Kind:** system · **Registry name:** `Input` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/systems/input.h`](../../../src/view/systems/input.h), [`src/view/systems/input.cpp`](../../../src/view/systems/input.cpp)

## Purpose
Owns the desktop input state. Its constructor emplaces the [`Input`](../components/Input.md) context variable, and every tick it drains the SDL event queue into it (action bits, mouse buttons, wheel, relative motion, text, window focus / minimize / resize, quit) and samples window size and mouse position. It is the first host system in every shipped rendered scene; scripted capture input ([Capture](Capture.md)) reaches it through the same SDL queue.

## Lifecycle
- **Construction / params:** `InputSys(entt::registry&)`, no JSON params (`systemInfo<InputSys>`). Emplaces `Input` in the constructor, i.e. while `Game::start` builds the SystemSet, before any preinit.
- **preinit:** None (default).
- **init:** None (default).
- **tick(dt):**
  1. Resets `leftClick`, `rightClick`, `middleClick` to false, `mouseWheel` and `mouseDelta` to (0, 0).
  2. Gets the renderer's `Context*` context variable.
  3. For each `SDL_PollEvent`:
     - window resized: `on.set(RESIZE)` (never cleared here);
     - focus gained / lost: `focused = true / false`;
     - restored / minimized: `window_showing = true / false`;
     - `SDL_QUIT` or key down of Escape: `on.set(EXIT)`;
     - other key down/up: if the scancode is in `actionMap` (W FORWARD, S BACKWARD, A LEFT, D RIGHT, P PAUSE, Space UP, LCtrl DOWN, LShift SPRINT, R VR_RIG, M MENU, K DEBUG, Backspace BACKSPACE, Left ROTATE_LEFT, Right ROTATE_RIGHT, Up ANGLE_UP, Down ANGLE_DOWN, Tab TAB) sets that bit to (event is key down);
     - mouse button down/up (left, right, middle): `mouseX = xDown = (down)`, and on down `xClick = true`;
     - mouse motion: `mouseDelta += (xrel, yrel)`;
     - mouse wheel: `mouseWheel = (wheel.x, wheel.y)` (last event wins, not summed);
     - text input: copies the text buffer into `text`.
  4. `screenSize = SDL_GetWindowSize(ctx.win)`.
  5. If `focused`, `mousePos = SDL_GetMouseState`.
- **finish:** None (default). Destructor is empty.

## Expected behavior
- Click flags, `mouseWheel` and `mouseDelta` are valid for exactly one frame; held flags and action bits persist until the matching release event.
- Key repeat events are not filtered: holding a key re-sets its bit on every SDL repeat (relevant for consumers that clear one-shot bits, such as PAUSE in [TimeSys](TimeSys.md)).
- Escape never reaches `actionMap`; it only sets EXIT.
- `mousePos` is frozen while the window is unfocused (e.g. hidden capture windows).
- `RESIZE` stays set until another system clears it (none in `src/view`).
- Only the last text-input event of a frame is kept.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Input`](../components/Input.md) | host | write +create | constructor | `reg.ctx().emplace<Input>()` |
| [`Input`](../components/Input.md) | host | readwrite | tick | all fields as described above |
| [`Context*`](../components/Context.md) | host | read | tick | `ctx.win` for `SDL_GetWindowSize` |

## Side effects
- Drains the whole SDL event queue (`SDL_PollEvent`); no other system may poll SDL events.
- Calls `SDL_GetWindowSize`, `SDL_GetMouseState`.

## Ordering and dependencies
- Must be constructed before anything reads `Input` (Game::start calls `reg.ctx().get<Input>()` after init; a rendered scene without `Input` fails there).
- Must tick before [UISys](UISys.md) and every input consumer ([PlayerControlSys](PlayerControlSys.md), [TimeSys](TimeSys.md), [BoxThrowerSys](BoxThrowerSys.md), [RollerPlayerSys](RollerPlayerSys.md)).
- Needs the renderer's `Context*` (emplaced by the renderer's `Context` constructor), hence `needs_renderer`.
- Main thread only (SDL).

## Related
[UISys](UISys.md), [Capture](Capture.md), [Game](Game.md), [`Input`](../components/Input.md), [folder `src/view/systems/`](../folders/src-view-systems.md).
