# ModelWatcherSys

> **Kind:** system · **Registry name:** `ModelWatcher` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** no
> **Source:** [`src/onnx/policy_systems.h`](../../../src/onnx/policy_systems.h), [`src/onnx/policy_systems.cpp`](../../../src/onnx/policy_systems.cpp), [`src/onnx/policy_runner.h`](../../../src/onnx/policy_runner.h), [`src/onnx/policy_runner.cpp`](../../../src/onnx/policy_runner.cpp)

## Purpose
Viewer host system that loads ONNX policies and injects them into the worlds as `PolicyHandle`, for `PolicySys` to run. Each world gets a model spec: a fixed `.onnx` file, or `latest:<dir>` for the newest `<behavior>-<step>.onnx` checkpoint of a training run, which is polled and hot-reloaded once the file is stable. Worlds with the same spec share one `PolicyRunner` ("feed"). The state of every world's policy is published in the host context variable `PolicySources`. Only built and registered with `DYNAMICAL_ONNX`.

## Lifecycle
- **Construction / params:** `ModelWatcherSys(reg, params)`; emplaces `PolicySources` in the host context. JSON params:
  - `model` (string, default `""`): spec of every world; empty means no policy.
  - `models` (object `{ "<world index>": "<spec>" }`, default none): per-world overrides; keys parsed with `std::stoul` (non-numeric keys throw), values must be strings (else nlohmann throws). Ignored if not an object.
  - `poll` (number, default 2.0): seconds (wall clock) between directory polls.
  - `deterministic` (bool, default true): copied into each `PolicyHandle`.
  - Spec syntax: `<file.onnx>` | `latest:<dir>` | `latest:<dir>|<fallback.onnx>`. One path component of `dir` may contain `*` (e.g. `latest:runs/*/WheelRoller`).
- **preinit / init:** none (host systems init before worlds configure their agents, so loading is deferred to the first tick).
- **tick(dt):** `dt` unused.
  1. First tick only, then return: `first()`:
     - `sources.worlds.assign(worlds count)` (`Worlds` context; 0 when absent); for each world `i`, spec = `models[i]` or `model`; stored in `sources.worlds[i].spec`; non-empty specs are grouped into feeds by identical string (a `latest:` spec is split into `dir` and optional `fallback` at `|`).
     - Each feed: plain file -> `load(feed, spec, stepOf(spec))`; `latest:` -> `latest(dir)`, loaded if found, else the fallback if any, else `error = "no <behavior>-<step>.onnx in <dir>"` for its worlds and a warning.
     - `last_poll = now`.
  2. Later ticks: return if no feed is a `latest:` feed, if `RunArgs::deterministic()`, or if less than `poll` seconds since `last_poll`. Else `last_poll = now`, and for each `latest:` feed: `file = latest(dir)`; skip if empty or equal to the loaded `feed.file`; read size and mtime (skip on error); if it equals the previous candidate (same path, size and time) -> `load` and clear the candidate, else remember it as the candidate.
- **load(feed, file, step):** layout from the first world of the feed: its `AgentIO` (`observation_size`, `action_size`); missing or `observation_size == 0` -> error "world N has no agent". `PolicyRunner(file, obs, act)` (1 thread) validates: file exists; one input `obs_0` of shape `[batch, n]` with `n == observation_size`; outputs `version_number`, `memory_size`, `continuous_action_output_shape`, `continuous_actions`, `deterministic_continuous_actions`; one evaluation on zeros reads the constants; `memory_size > 0` (recurrent) refused; action count must equal `action_size`. On success: `insert_or_assign(PolicyHandle{ runner, deterministic })` in each feed world, `sources.worlds[i] = { file, step, error cleared }`, `feed.file = file`, info log. On `PolicyError`: `error = "<filename>: <reason>"` for each feed world and an error log; the previous handle (if any) stays.
- **latest(dir, step):** without `*`: the regular file of `dir` named `<name>-<digits>.onnx` with the largest step (files without a step are ignored; missing dir -> empty). With `*` in a component: for each directory under the prefix whose name matches the glob, the largest-step file of `<match>/<rest>`; returns the one with the most recent `last_write_time`, and its step.
- **finish:** none.

## Expected behavior
- Policies are injected on the first host tick, before the first world step if `ModelWatcher` precedes `WorldStepper` (as in `vr_roller`).
- A checkpoint is loaded only after two consecutive polls (>= `poll` s apart) see the same path, size and mtime; a newer checkpoint replaces the handle in all worlds of the feed at once.
- No polling (only the initial load) in deterministic / fixed-clock runs or when all specs are plain files.
- A failing checkpoint is not recorded as loaded, so it becomes a candidate again and is retried (and logged) every second poll until a newer one appears.
- `PolicySource::step` is -1 for files without `-<step>` in the name.
- Worlds with an empty spec get no handle and keep a default `PolicySource` with only `spec` set.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [PolicySources](../components/PolicySources.md) | host | write +emplace_ctx | construct | empty |
| [PolicySources](../components/PolicySources.md) | host | readwrite | tick | `worlds[i].spec/file/step/error` |
| [Worlds](../components/Worlds.md) | host | read | tick | `size()`, `operator[]` to reach world registries |
| [RunArgs](../components/RunArgs.md) | host | read | tick | `deterministic()` disables polling |
| [AgentIO](../components/AgentIO.md) | world | read | tick | `observation_size`, `action_size` of the feed's first world |
| [PolicyHandle](../components/PolicyHandle.md) | world | write +emplace_ctx | tick | `insert_or_assign` in each feed world |

## Side effects
- Filesystem: directory iteration, `file_size`, `last_write_time`; reads `.onnx` files.
- Creates ONNX Runtime sessions (process-wide `Ort::Env`, graph optimisation `ORT_ENABLE_ALL`) and evaluates each model once at load.
- Wall-clock timer (`steady_clock`) for polling.
- Logs: info on load, warning when a `latest:` dir has no checkpoint, error on load failure.

## Ordering and dependencies
- Before `WorldStepper` in the host Pre lane so handles are in place before world ticks and never written while worlds step in parallel.
- After the worlds exist and their agents configured `AgentIO` (worlds' `init`, which runs after host `init`); hence the deferred first tick.
- Main thread.

## Related
[PolicySys](PolicySys.md), [PolicyHandle](../components/PolicyHandle.md), [PolicySources](../components/PolicySources.md), [AgentIO](../components/AgentIO.md), [`src/onnx/`](../folders/src-onnx.md), [inference-and-replay.md](../../physical-agents/inference-and-replay.md)
