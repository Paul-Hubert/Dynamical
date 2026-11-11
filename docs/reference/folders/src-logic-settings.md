# `src/logic/settings/`

> [Source](../../../src/logic/settings/) · Parent: [`src/logic/`](src-logic.md)

## Summary
The user settings of `dynamical`: window size and mode, frame cap, VR mode, chunk view distance and a few legacy network fields. `Game` emplaces a `Settings(argc, argv)` host context variable, which loads `DYNAMICAL_CONFIG_DIR "config.6.json"` (default `./config.6.json`) with cereal if present, rewrites the file with the current values, then applies `key=value` command-line overrides. `dynamical_train` and the tests have no `Settings`; ChunkManagerSys falls back to `Settings::default_view_distance` (768).

## Files
| File | Contents |
|---|---|
| [`settings.h`](../../../src/logic/settings/settings.h) | `Settings` fields with defaults, `default_view_distance`, cereal `serialize` (all ten fields), config file macros (`DYNAMICAL_CONFIG_DIR`, `DYNAMICAL_CONFIG_MAGIC` "6", `DYNAMICAL_CONFIG_FILE`). |
| [`settings.cpp`](../../../src/logic/settings/settings.cpp) | `load()` (read if the file exists, then always `save()`), `save()`, constructor, `argument_override` (window_width, window_height, fullscreen, fps_max, username, server_side, client_side, vr_mode, view_distance; not spectator_mode; booleans true only for the text "true", ints via `atoi`). |

## Systems defined here
None.

## Components defined here
- [Settings](../components/Settings.md)

## Subfolders
None.
