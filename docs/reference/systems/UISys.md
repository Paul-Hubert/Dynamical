# UISys

> **Kind:** system · **Registry name:** `UI` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/systems/ui.h`](../../../src/view/systems/ui.h), [`src/view/systems/ui.cpp`](../../../src/view/systems/ui.cpp)

## Purpose
Bridges [`Input`](../components/Input.md) to Dear ImGui: writes mouse buttons, position, display size, text and Backspace/Tab key events into `ImGuiIO`, calls `ImGui::NewFrame()`, and then hides mouse clicks from the game while ImGui wants the mouse. The ImGui frame is rendered later by the renderer's UI pass.

## Lifecycle
- **Construction / params:** `UISys(entt::registry&)`, no JSON params. Creates eight SDL system cursors into `g_MouseCursors` (never used or freed afterwards). Creates an ImGui context with the dark style only if none exists (normally the renderer's UI pass already created it for the font atlas).
- **preinit:** None.
- **init:** None.
- **tick(dt):**
  1. `io.MouseDown[0..2] = input.mouseLeft / mouseRight / mouseMiddle`.
  2. If `input.text` holds a value, `io.AddInputCharactersUTF8` and reset `input.text`.
  3. `io.AddKeyEvent(ImGuiKey_Backspace, on[BACKSPACE])`, `io.AddKeyEvent(ImGuiKey_Tab, on[TAB])`.
  4. If `input.focused`, `io.MousePos = input.mousePos`.
  5. `io.DisplaySize = input.screenSize`.
  6. `io.DeltaTime` from the SDL performance counter since the previous tick (1/60 on the first tick); overridden with `dt` when `RunArgs::deterministic()`.
  7. `ImGui::NewFrame()`.
  8. If `io.WantCaptureMouse`: clear `leftClick/rightClick/middleClick` and `leftDown/rightDown/middleDown`; else `leftDown/rightDown/middleDown = mouseLeft/mouseRight/mouseMiddle`.
- **finish:** None. Destructor is empty.

## Expected behavior
- After UISys, `Input::xClick`/`xDown` are false whenever ImGui wants the mouse, so game systems see no clicks over ImGui windows.
- `io.MousePos` is not touched while unfocused, so a value written by [Capture](Capture.md) (scripted `mouse`) survives in hidden capture windows.
- Deterministic runs feed ImGui the fixed frame dt (reproducible UI animation).
- `WantCaptureMouse` is the value computed by `NewFrame` in this tick.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Input`](../components/Input.md) | host | readwrite | tick | reads mouse/text/keys/focus/size; resets `text`; writes click and down flags |
| [`RunArgs`](../components/RunArgs.md) | host | read | tick | `find`, `deterministic()` |

## Side effects
- `ImGui::NewFrame()` and `ImGuiIO` writes (global ImGui state).
- Possibly `ImGui::CreateContext()` in the constructor.
- `SDL_CreateSystemCursor` x8 in the constructor (leaked; unused).
- Reads `SDL_GetPerformanceCounter` (wall clock) unless deterministic.

## Ordering and dependencies
- After [InputSys](InputSys.md) (needs this frame's `Input`).
- Before every system that issues ImGui calls ([TimeSys](TimeSys.md), [DebugMenuSys](DebugMenuSys.md), [WorldStepperSys](WorldStepperSys.md), [SphereViewSys](SphereViewSys.md)) or reads `WantCapture*` ([PlayerControlSys](PlayerControlSys.md), [BoxThrowerSys](BoxThrowerSys.md), [RollerPlayerSys](RollerPlayerSys.md)).
- ImGui is not thread safe: main thread only.

## Related
[InputSys](InputSys.md), [Capture](Capture.md), [`Input`](../components/Input.md), [folder `src/view/systems/`](../folders/src-view-systems.md).
