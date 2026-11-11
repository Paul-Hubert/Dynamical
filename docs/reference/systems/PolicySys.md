# PolicySys

> **Kind:** system · **Registry name:** `Policy` · **Scope:** World · **Lane:** Fixed (Phase Act) · **Needs renderer:** no
> **Source:** [`src/onnx/policy_systems.h`](../../../src/onnx/policy_systems.h), [`src/onnx/policy_systems.cpp`](../../../src/onnx/policy_systems.cpp), [`src/onnx/policy_runner.h`](../../../src/onnx/policy_runner.h), [`src/onnx/policy_runner.cpp`](../../../src/onnx/policy_runner.cpp)

## Purpose
Local inference in place of the trainer: on the first tick of each decision window it runs the world's `PolicyHandle` (an ONNX ML-Agents policy through ONNX Runtime) on the agent's last observation (`AgentIO::observation`) and writes the window's action (`AgentIO::action`). Scenes put it in the slot of `TrainerActions@act` (e.g. `vr_roller` replaces `TrainerActions@act` with `Policy@act`). Only built and registered with `DYNAMICAL_ONNX` (`registerPolicySystems()`).

## Lifecycle
- **Construction / params:** `PolicySys(entt::registry&)`; no JSON params.
- **preinit / init:** none.
- **tick(h):** `h` unused.
  1. `handle = ctx().find<PolicyHandle>()`, `io = ctx().find<AgentIO>()`. Return if either is missing or `handle->runner` is null.
  2. Return if `io->observation.size() != runner->info().observation_size` or `io->action.size() != info.action_size`.
  3. `runner->run(observation.data(), 1, action.data(), handle->deterministic)`: batch of 1; output `deterministic_continuous_actions` (mean) when deterministic, else `continuous_actions` (mean + graph noise); both already clamped to [-1, 1] by the exported graph (`clamp(-3, 3) / 3`). Copies `action_size` floats into `action`.
- **finish:** none.

## Expected behavior
- Without a handle (no ModelWatcher, model failed to load) or with a layout mismatch, the action is left unchanged (zeros after `AgentIO::configure`).
- Swapping the handle (ModelWatcher `insert_or_assign`) takes effect at the next Act tick.
- Deterministic output for a deterministic handle (same observation, same action).
- `PolicyRunner::run` can throw `Ort::Exception` (not caught here); not expected for a model that passed load validation.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [PolicyHandle](../components/PolicyHandle.md) | world | read | tick | `runner`, `deterministic` |
| [AgentIO](../components/AgentIO.md) | world | readwrite | tick | `observation` read, `action` written |

## Side effects
- ONNX Runtime inference (CPU, 1 intra-op thread per session as created by ModelWatcher) on the world's thread. Several worlds may call the same shared `PolicyRunner` concurrently (`Ort::Session::Run` is thread-safe).

## Ordering and dependencies
- Act phase: after the previous window's `Observe` systems wrote `AgentIO::observation` (or the reset observation), before the control system that applies `AgentIO::action` (`SphereControl`).
- `PolicyHandle` is written by `ModelWatcherSys` on the host between world steps (it runs before `WorldStepper` in the host Pre lane), so there is no concurrent write.

## Related
[ModelWatcherSys](ModelWatcherSys.md), [PolicyHandle](../components/PolicyHandle.md), [AgentIO](../components/AgentIO.md), [`src/onnx/`](../folders/src-onnx.md), [inference-and-replay.md](../../physical-agents/inference-and-replay.md)
