# `src/extra/optick/`

> [Source](../../../src/extra/optick/) · Parent: [`src/extra/`](src-extra.md)

## Summary
A vendored copy of the Optick C++ profiler (core, platform backends for Windows/Linux/macOS, Vulkan and D3D12 GPU hooks, message server and serialisation). The engine includes `extra/optick/optick.h` for the `OPTICK_EVENT` / `OPTICK_FRAME` macros (`logic/systems/system.h`, `main.cpp`, `view/game.cpp`, `renderer/renderer.cpp`). Several files of the copy are empty (`optick.config.h`, `optick_capi.h`, `optick_core.platform.h`, `optick_core.freebsd.h`, `optick_miniz.h/.cpp`); `optick.h` includes the empty `optick.config.h` and guards everything with `#if USE_OPTICK`; nothing in the sources or `CMakeLists.txt` defines `USE_OPTICK`, so the `#else` branch applies and `OPTICK_EVENT(...)` and the other macros expand to nothing (unless a build passes `USE_OPTICK` on the command line). Not documented per file.

## Files
Vendored Optick sources (`optick.h`, `optick_common.h`, `optick_core*`, `optick_gpu*`, `optick_memory.h`, `optick_message*`, `optick_serialization*`, `optick_server*`, `optick_capi.cpp`, `optick_miniz*`); no per-file detail.

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
