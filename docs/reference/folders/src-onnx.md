# src/onnx

Inference of the policies the ML-Agents trainer exports (ONNX, opset 9) with ONNX Runtime on the CPU
([inference and replay](../../physical-agents/inference-and-replay.md)). Built as the `dynamical_onnx` library when
ONNX Runtime is found (`DYNAMICAL_ONNX`); `dynamical` and `dynamical_train` call `registerPolicySystems()` to get its
systems.

## Files

| File | What |
|---|---|
| [`policy_runner.h`](../../../src/onnx/policy_runner.h), [`policy_runner.cpp`](../../../src/onnx/policy_runner.cpp) | `PolicyRunner` (one vector observation `obs_0` in, deterministic or sampled continuous actions out; layout checked at load, recurrent models refused, `run` thread safe), `PolicyError`, `PolicyHandle` (world context), `makePolicyHandle` |
| [`agent_policy.h`](../../../src/onnx/agent_policy.h), [`agent_policy.cpp`](../../../src/onnx/agent_policy.cpp) | `AgentPolicy` (any layout: `obs_0`, `obs_1`... for the sensors, buffers as [rows, size], action masks, continuous and discrete outputs, checked against the agent at its first run), `findBehaviorModel`, system Policies, the process-wide session cache, `makePolicyHandle`'s body |
| [`policy_systems.h`](../../../src/onnx/policy_systems.h), [`policy_systems.cpp`](../../../src/onnx/policy_systems.cpp) | systems Policy and ModelWatcher, `PolicySource`, `PolicySources` (host context), `ModelWatcherSys::latest`, `registerPolicySystems` |

## Systems

- [Policy](../systems/Policy.md) (World, Act): the world's PolicyHandle on its AgentIO context
- [Policies](../systems/Policies.md) (World, Act): every AgentIO component that asks, by behaviour, from a run folder
- [ModelWatcher](../systems/ModelWatcher.md) (Host): loads files or the newest checkpoints and injects PolicyHandles
  into the worlds

## Components and context variables

- [PolicyHandle](../components/PolicyHandle.md): world context, the policy of the world's agent (`runner` or `agent`)
- [PolicySources](../components/PolicySources.md): host context, what each world runs (for panels)

## Worth knowing

- Two kinds of agents, two runners: an agent that is a world's context variable with one vector observation and
  continuous actions takes a `PolicyRunner`; one with sensors (a walker's order sensor) or discrete branches, or
  that is a component, takes an `AgentPolicy`. `makePolicyHandle(onnx, io)` picks from the AgentIO's layout.
- Sessions are shared: `AgentPolicy` sessions through a static cache keyed by canonical path and write time
  (weak pointers, under a mutex), so many worlds of a scene run one session; `Run` is thread safe, worlds step in
  parallel.
- Model files: `latest:<dir>` specs pick `<behavior>-<step>.onnx` of the largest step (with a `*` directory
  component, of the most recent run); `findBehaviorModel` prefers the end-of-run export `<behavior>.onnx`.
- Outputs read: `deterministic_continuous_actions` / `continuous_actions` (already clamped to -1..1 by the export),
  `deterministic_discrete_actions` / `discrete_actions` (int64, int32 or float).

## Dependencies

- ONNX Runtime (`onnxruntime_cxx_api.h`), `src/logic/agents/agent_io.h`, `src/scene/` (`Worlds`, `RunArgs`,
  `registerSystem`), `src/util/log.h`.
- Used by `src/main.cpp`, `src/train_main.cpp`, `src/train/train_host.cpp` and `animal_host.cpp` (mode=eval),
  `src/view/sphere_view.cpp` (PolicySources), the tests in `tests/onnx/`.

## Design docs

- [docs/physical-agents/inference-and-replay.md](../../physical-agents/inference-and-replay.md)
- [docs/action-agents.md](../../action-agents.md) (inference of the sandbox's agents)
