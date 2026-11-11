# `tests/`

> [Source](../../../tests/) · Parent: repository root

## Summary
The doctest suite `dynamical_tests` plus CMake-script and Python-driven tests. `CMakeLists.txt` (with `DYNAMICAL_TESTS`) globs `tests/**/*.cpp` into one executable linked against `dynamical_sim` and `imgdiff_core`, leaving out `tests/protocol/` and `tests/support/mock_trainer*` without `DYNAMICAL_TRAIN` and `tests/onnx/` without `DYNAMICAL_ONNX`; with `DYNAMICAL_TRAIN` it also links `dynamical_train_core` and defines `DYNAMICAL_TRAIN_EXE`. `doctest_discover_tests(... ADD_LABELS 1 WORKING_DIRECTORY <repo root>)` turns each `TEST_CASE` into a CTest test labelled by its `TEST_SUITE` (`unit`, `scenario`, `protocol`); tests open their data by repo-relative paths (`tests/data/...`). Additional CTest entries: `T1.2` (`import_table.cmake`, MSVC only), `T1.7` (`train_cwd.cmake`), `L3.1` (`tools/train_smoke.py`, label `slow`, only when `build/venv` exists) and, with the viewer, the `gpu`-labelled visual and probe tests run through `tools/visual_test.py` against `tests/capture/` scripts and `tests/golden/`. Test IDs (T, S, V, P, SR, L, B) refer to `docs/physical-agents/phases.md` / `testing.md`.

## Files
| File | Contents |
|---|---|
| `main.cpp` | doctest runner: creates a `JoltRuntime`, calls `registerSimSystems()` and, with `DYNAMICAL_ONNX`, `registerPolicySystems()` |
| `import_table.cmake` | T1.2: `dumpbin /dependents` of TrainingEnv.exe must not list vulkan-1, SDL2 or openxr_loader |
| `train_cwd.cmake` | T1.7: run dynamical_train from another directory; relative `-envParamsOut` lands there, resources found next to the executable, no `config.6.json` created |

## Systems defined here
None. (`tests/support/test_systems.*` defines stub systems for the loader tests.)

## Components defined here
None in `src/`; `tests/support/test_systems.h` defines a `CallLog` context variable used only by tests.

## Subfolders
[`unit/`](tests-unit.md), [`protocol/`](tests-protocol.md), [`onnx/`](tests-onnx.md), [`support/`](tests-support.md), [`data/`](tests-data.md), [`golden/`](tests-golden.md), [`capture/`](tests-capture.md)
