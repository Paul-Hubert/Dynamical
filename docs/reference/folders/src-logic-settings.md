# src/logic/settings

The settings of `dynamical`: the config file with its command line overrides, and the graphics levels of
`resources/graphics.json`.

| File | What |
|---|---|
| [`settings.h`](../../../src/logic/settings/settings.h) / [`settings.cpp`](../../../src/logic/settings/settings.cpp) | `Settings`: window, fps, VR mode, view distance (768), graphics level, grass kind, ground relief; `load` (`./config.7.json` via cereal, then rewritten), `save`, `argument_override` (`key=value`) |
| [`graphics.h`](../../../src/logic/settings/graphics.h) / [`graphics.cpp`](../../../src/logic/settings/graphics.cpp) | `GraphicsPreset` (one level: view distance, ray tracing, lighting per target, probe rays, far plants, textures, relief, grass layers / shadow / see-through, LOD bias, normal maps, subsurface, streaming), context `Graphics`, `loadGraphicsPresets` (warnings on a missing or bad file), `findGraphicsPreset`, `applyGraphicsPreset` |

## Systems registered here

None. `Game` emplaces both context variables ([service-Game](../systems/service-Game.md)); the `Renderer` applies the
current level to `RenderSettings`; the debug menu switches levels; [ChunkManager](../systems/ChunkManager.md) reads
`Settings::view_distance`.

## Components and context defined here

- [Settings](../components/Settings.md) (host context, dynamical only)
- [Graphics](../components/Graphics.md) (host context, dynamical only)

## Main functions

- `Settings(argc, argv)`: load, save, then the overrides (only the serialised fields are saved: `grass_kind` and
  `ground_relief` never are).
- `applyGraphicsPreset(preset, settings)`: everything of a level that lives in `RenderSettings`.

## Dependencies

[`src/logic/components`](src-logic-components.md) (`RenderSettings`), cereal, nlohmann-json, [`src/util`](src-util.md) (log).

## Design docs

[docs/graphics.md](../../graphics.md).

## Tests

`GRAPHICS.1` to `GRAPHICS.4` ([`tests/unit/graphics.cpp`](../../../tests/unit/graphics.cpp)).
