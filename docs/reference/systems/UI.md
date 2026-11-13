# UI

`UISys` in [`src/view/systems/ui.cpp`](../../../src/view/systems/ui.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre (after Input) · Parameters: no

The ImGui platform layer: passes the frame's mouse, keys, text, display size and delta time to ImGui, starts the ImGui
frame, and hides the mouse buttons from the scene while the cursor is over an ImGui window. The renderer's UI pass draws
the frame.

## Behaviour

- **Constructor**: creates eight SDL system cursors (arrow, I-beam, size all / NS / EW / NESW / NWSE, hand), kept in
  the system and not used further; creates the ImGui context with the dark style if the UI pass has not already made it
  (it needs one for its font atlas).
- **tick**:
  1. `io.MouseDown[0..2]` = `mouseLeft`, `mouseRight`, `mouseMiddle`; typed `text` is given to ImGui and cleared;
     Backspace and Tab key events from `Input::BACKSPACE` / `TAB`; `io.MousePos` from `mousePos` while focused;
     `io.DisplaySize` from `screenSize`.
  2. `io.DeltaTime` from SDL's performance counter (1/60 s the first frame); with a deterministic run
     (`RunArgs::deterministic()`, captures and probes) the frame's `dt` instead, so ImGui behaves the same every run.
  3. `ImGui::NewFrame()`.
  4. If `io.WantCaptureMouse`: clears `leftClick`, `rightClick`, `middleClick`, `leftDown`, `rightDown`, `middleDown`;
     else copies `mouseLeft/Right/Middle` into `leftDown/rightDown/middleDown`. Systems that act on clicks read the
     `*Click` / `*Down` fields so that clicks on the menus do not reach the world.
- **Destructor**: nothing (the cursors are not freed).

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Input](../components/Input.md) | context | readwrite | tick | reads buttons, text, keys, mouse position, screen size; consumes text; clears leftClick/rightClick/middleClick and *Down when ImGui wants the mouse, else copies mouse* into *Down |
| [RunArgs](../components/RunArgs.md) | context | read | tick | deterministic(): ImGui's DeltaTime is the frame's dt instead of the wall clock |

## Side effects

- Creates the ImGui context (dark style) in its constructor when the renderer's UI pass has not made it yet.
- Creates eight SDL system cursors in its constructor (never set nor freed).
- Calls ImGui::NewFrame every tick: every system that draws an ImGui window must come after it in the frame; the UI pass renders the frame.

## Order and dependencies

After Input (reads what it wrote this frame), before every system that calls ImGui (DebugMenu, Time, WorldStepper,
the view systems' panels) and before the systems that read `leftClick` / `leftDown` (BoxThrower, PlayerControl).

## Used in scenes

<!-- generated -->

## Tests

- `T2.4 base system list after the PhysicsSys split` (`tests/unit/base_scene.cpp`, stub).
- GPU capture tests: `P2 check: scripted clicks pause and single-step WorldStepper` (clicks on the Worlds panel go
  through ImGui), `P1 check: scripted input ...`.
