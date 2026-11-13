# Sky

`SkySys` in [`src/logic/sky/sky.cpp`](../../../src/logic/sky/sky.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre` (before the chunk systems in the terrain scenes) · Parameters: yes

Sets the time of the day of the scene and moves it ([lighting](../../lighting.md)). The
[Sky](../components/Sky.md) context it writes is all the lighting needs: `skyLight(Sky)` gives the sun and the
moon (from the hour angle, the declination and the latitude; east +X, north -Z), the one directional light (the
brighter of the two, through zero when one takes over), the colours of the sky and the ambient light, mirrored by
`shaders/include/view.glsl`. A day lasts 24 minutes by default.

## Behaviour

**constructor**: a Sky with `time`, `latitude`, `declination` from the parameters (defaults 10.3349 h, 42.8°,
14.787°: the middle of an August morning at Tautavel, where the sun was before the system existed). `day_length`:
the parameter when it is 0 or more; left out or negative, 1440 s, but 0 (the sun stopped) in runs with a fixed
frame clock (`RunArgs::deterministic()`: captures, probes), so that they keep comparing with their goldens.
`insert_or_assign` into the context, so the sky is there before any init.

**tick**: with `day_length > 0`, `time += dt x 24 / day_length`, wrapped to [0, 24).

Without the system the renderer's `Lights` emplaces a default Sky (time 10.3349, `day_length` 0): the sun stays
where the shaders had it.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `time` | float | 10.3349 | hours |
| `day_length` | float | 1440 (0 with a fixed frame clock) | seconds of play for a day; 0 stops the sun |
| `latitude` | float | 42.8 | degrees |
| `declination` | float | 14.787 | degrees: 23.4 midsummer, 0 the equinoxes, -23.4 midwinter |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Sky](../components/Sky.md) | context | write+create | constructor | insert_or_assign with the scene's time, latitude, declination and day length |
| [Sky](../components/Sky.md) | context | readwrite | tick | time += dt x 24 / day_length, wrapped to [0, 24) |
| [RunArgs](../components/RunArgs.md) | context | read | constructor | a fixed frame clock (captures, probes) makes the default day length 0 |

## Side effects

- None.

## Order and dependencies

- Read by the renderer (`Lights`, `TreeShade`, the sky pass, probes) in `prepare` / `render`, after the tick; its
  place in `host.pre` does not matter to the other systems. The debug menu may change `time` while running.

## Used in scenes
<!-- generated -->

## Tests

- `LIGHT.1` to `LIGHT.4` in [`tests/unit/lighting.cpp`](../../../tests/unit/lighting.cpp): the default sun, one
  light at a time, the sky colours, the system starts where the scene says and turns the sun a day in
  `day_length`.
- `T2.4` in [`tests/unit/base_scene.cpp`](../../../tests/unit/base_scene.cpp) (base scene system list); captures
  `tests/capture/lighting*.capture.json`.
