# `cmake/`

> [Source](../../../cmake/) · Parent: repository root

## Summary
CMake helper scripts included or run by the top-level `CMakeLists.txt`.

## Files
| File | Contents |
|---|---|
| `git_version.cmake` | script mode (`-DSOURCE_DIR -DOUT -P`): writes a header defining `DYNAMICAL_GIT_SHA` from `git rev-parse --short=9 HEAD` (`+dirty` with tracked changes, `unknown` without git), only when it changed; stamped into `envparams.txt` by `TrainHost` |
| `onnxruntime.cmake` | downloads the prebuilt ONNX Runtime CPU release (1.23.2, win-x64) into `<build>/_deps`, pinned by SHA-256; defines `onnxruntime::onnxruntime` and `ONNXRUNTIME_DLLS` |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
