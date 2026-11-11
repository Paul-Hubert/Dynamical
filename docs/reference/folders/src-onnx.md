# `src/onnx/`

> [Source](../../../src/onnx/) · Parent: [`src/`](src.md)

## Summary
Policy inference with ONNX Runtime, compiled into the optional `dynamical_onnx` static library (CMake option `DYNAMICAL_ONNX`, default on for Windows; defines `DYNAMICAL_ONNX`, which makes `main.cpp` call `registerPolicySystems()`). `PolicyRunner` loads an ML-Agents ONNX export, validates its layout against the agent's observation/action sizes and runs it on the CPU. In a viewer scene, the host system `ModelWatcherSys` resolves model specs (files or the newest checkpoint of a training run, hot-reloaded) and injects a `PolicyHandle` into each world; the world system `PolicySys` runs it in the Act phase in place of the trainer's actions.

## Files
| File | Contents |
|---|---|
| [`policy_runner.h`](../../../src/onnx/policy_runner.h) / [`.cpp`](../../../src/onnx/policy_runner.cpp) | `PolicyRunner` (load and validate `obs_0` -> `deterministic_continuous_actions` / `continuous_actions`, refuse recurrent models, `run(obs, batch, actions, deterministic)`, `Info`), `PolicyError`, `PolicyHandle` world context variable; one process-wide `Ort::Env`. |
| [`policy_systems.h`](../../../src/onnx/policy_systems.h) / [`.cpp`](../../../src/onnx/policy_systems.cpp) | `PolicySys`, `PolicySource`, `PolicySources` host context variable, `ModelWatcherSys` (spec parsing, `latest()` checkpoint search with one `*` component, stable-file polling), `registerPolicySystems()` (`Policy` World, `ModelWatcher` Host). |

## Systems defined here
- [PolicySys](../systems/PolicySys.md)
- [ModelWatcherSys](../systems/ModelWatcherSys.md)

## Components defined here
- [PolicyHandle](../components/PolicyHandle.md) (world context)
- [PolicySources](../components/PolicySources.md) (host context)

## Subfolders
None.
