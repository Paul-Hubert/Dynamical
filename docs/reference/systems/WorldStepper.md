# WorldStepper

`WorldStepperSys` in [`src/view/world_stepper.cpp`](../../../src/view/world_stepper.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: no

In the viewer (`dynamical`), the scene's worlds (the `Worlds` context variable `Game::start` makes from the scene's
`worlds`) are not stepped by a trainer: this system steps them in real time from the frame time, at the worlds' fixed
tick, at most 8 ticks a frame, and publishes in [WorldStepperState](../components/WorldStepperState.md) how far the
frame is between the last two ticks, so that WorldView and the view systems draw the bodies smoothly. Its "Worlds"
panel pauses them and steps one tick at a time. Under capture the frame dt is fixed (`fixed_frame_dt`), so the ticks of
a frame, and the captures, are reproducible.

## Behaviour

- **preinit**: emplaces `WorldStepperState`.
- **tick** (returns at once without `Worlds` or with no world):
  1. `h` = `WorldClock::dt` of world 0 (all worlds share the scene's rate).
  2. Running: accumulator += min(dt, 0.25); ticks = min(floor(accumulator / h), 8); accumulator -= ticks x h; when
     more than h is still left (too slow to keep up), the accumulator is dropped to 0. Paused: 1 tick if `step_once`
     (then cleared), else 0.
  3. `worlds->step(ticks)`: every world advances that many fixed ticks, one after the other on the main thread.
  4. `ticks` total += ticks; `alpha` = accumulator / h (1 when paused).
  5. ImGui window "Worlds" at (10, 380) on first use: count, tick, h; Pause checkbox; Step button (pauses and steps
     one tick).

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [WorldStepperState](../components/WorldStepperState.md) | context | readwrite+create | preinit (emplace), tick | h from world 0's clock, paused / step_once from the panel, ticks, alpha = accumulator / h (1 when paused) |
| [Worlds](../components/Worlds.md) | context | readwrite | tick | worlds->step(ticks): every world runs that many fixed ticks |
| [WorldClock](../components/WorldClock.md) | context | read | tick (world 0's registry) | dt: the fixed step h |

## Side effects

- Runs every world's fixed systems (physics, agents, ...) on the main thread, sequentially (no executor passed to Worlds::step).
- Frame dt is clamped to 0.25 s; when more than h is left after 8 ticks the remaining time is dropped (no spiral).
- ImGui window "Worlds" (world count, tick, h, Pause checkbox, Step button).

## Order and dependencies

- After UI (panel). Before the systems that read this frame's world state or alpha: SphereView, VRRoller,
  DesktopRoller, WalkerView, GaitView, AnimalAgentView, SandboxWatch (pre) and WorldView (post). SR.18 checks
  WorldStepper before VRRoller and DesktopRoller.
- In `vr_roller` and the sphere scenes, ModelWatcher (policy reloads) comes before it.
- The worlds' own systems run inside `Worlds::step` (World::tick with its act / observe phases).

## Used in scenes

<!-- generated -->

## Tests

- GPU: `P2 check: scripted clicks pause and single-step WorldStepper` (`tests/capture/worlds_stepper.capture.json`:
  clicks Pause then Step on the panel, shots before and after), `V2.1 test_boxes_view at frame 300 matches its golden`.
- `SR.18 vr_roller: ...` (`tests/onnx/vr_roller.cpp`, order in the scene).
