# TimeSys

> **Kind:** system · **Registry name:** `Time` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/systems/system_list.h`](../../../src/view/systems/system_list.h), [`src/view/systems/time.cpp`](../../../src/view/systems/time.cpp), [`src/view/components/time.h`](../../../src/view/components/time.h)

## Purpose
Legacy calendar clock from the archived tile-map game. Keeps a [`Time`](../components/Time.md) context variable in integer game seconds, toggles a pause with P, offers speed radio buttons in a top-right ImGui window, and shows a separate "Frame time" window. Nothing else in the engine reads `Time`.

## Lifecycle
- **Construction / params:** `DEFINE_SYSTEM(TimeSys)`, no JSON params.
- **preinit:** `reg.ctx().emplace<Time>()`.
- **init:** None (empty).
- **tick(dt):**
  1. If `Input::on[PAUSE]`: `speed_modifier = last_speed_modifier` if it was 0, else 0; clear `on[PAUSE]`.
  2. ImGui window "Time" (no title bar, no resize, no move, function-static `open` flag), pinned to `(screenSize.x - width, 0)`: shows the `Date` breakdown of `current` (day+1, month name, year, h:m:s) and radio buttons "0".."4" mapping to modifiers 0, 1, 3, 10, 50; writes the selection to `speed_modifier`; if non-zero stores it in `last_speed_modifier`.
  3. `time.dt = speed * speed_modifier * dt * 10` (converted to `uint64_t`, truncating), `current += dt`.
  4. ImGui window "Frame time": `dt` in ms and fps.
- **finish:** None (empty).

## Expected behavior
- With defaults (speed 60, modifier 1) game time advances 600 game seconds per real second.
- P toggles pause once per key-down event; since [InputSys](InputSys.md) does not filter key repeat, holding P may toggle repeatedly.
- The radio buttons are evaluated every frame, so the step-1 toggle is applied before the buttons read `speed_modifier` back (same frame).
- Fractional game seconds are lost each frame (integer truncation): at very small `speed * modifier * dt * 10` (< 1) time does not advance.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Time`](../components/Time.md) | host | write +create | preinit | `emplace<Time>()` |
| [`Time`](../components/Time.md) | host | readwrite | tick | `speed_modifier`, `last_speed_modifier`, `dt`, `current` |
| [`Input`](../components/Input.md) | host | readwrite | tick | reads and clears `on[PAUSE]`; reads `screenSize.x` |

## Side effects
- ImGui windows "Time" and "Frame time".

## Ordering and dependencies
- After [InputSys](InputSys.md) and [UISys](UISys.md) (ImGui frame must be started).

## Related
[`Time`](../components/Time.md), [DebugMenuSys](DebugMenuSys.md), [folder `src/view/systems/`](../folders/src-view-systems.md).
