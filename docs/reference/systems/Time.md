# Time

`TimeSys` in [`src/view/systems/time.cpp`](../../../src/view/systems/time.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: no

The game calendar inherited from the tile map game: a [Time](../components/Time.md) context variable counting game
seconds at a chosen speed, a small "Time" window at the top right with the date and the speed, P to pause, and the
"Frame time" window. Nothing else in the engine reads `Time` (the sun has its own clock, [Sky](Sky.md)).

## Behaviour

- **preinit**: emplaces `Time` (current 0, speed 60 game s per real s, modifier 1).
- **tick**:
  1. `Input::PAUSE` set: toggles `speed_modifier` between 0 and `last_speed_modifier`, clears the flag.
  2. ImGui window "Time" (no title bar, placed at the top right from `Input::screenSize`): the date (`Date`: day of
     the season, season name, year, h:m:s) and radio buttons for the modifier (0, 1, 3, 10, 50, labelled 0 to 4);
     `last_speed_modifier` keeps the last non-zero one.
  3. `dt = speed * speed_modifier * dt * 10` (converted to the integer `time_t`), `current += dt`.
  4. ImGui window "Frame time": milliseconds and frames per second of the frame.
- **init**, **finish**: nothing.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Time](../components/Time.md) | context | readwrite+create | preinit (emplace), tick | speed_modifier from P and the radio buttons, last_speed_modifier, dt = speed x modifier x dt x 10, current += dt |
| [Input](../components/Input.md) | context | readwrite | tick | reads Input::PAUSE and clears it once used; screenSize places the window |

## Side effects

- Two ImGui windows: "Time" (date, speed radio buttons 0/1/3/10/50) at the top right, and "Frame time" (ms, fps).

## Order and dependencies

After Input (P) and UI (ImGui frame).

## Used in scenes

<!-- generated -->

## Tests

- `T2.4 base system list after the PhysicsSys split` (`tests/unit/base_scene.cpp`, stub); base captures (`V0.x`,
  `V1.1`, `V2.2`) draw its windows.
