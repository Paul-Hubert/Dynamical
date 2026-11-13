# Policy

`PolicySys` in [`src/onnx/policy_systems.cpp`](../../../src/onnx/policy_systems.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, `Policy@act` (replacing `TrainerActions@act` of a training template) · Parameters: no

Runs an exported ML-Agents policy (ONNX) on the CPU for a world whose agent is its
[AgentIO](../components/AgentIO.md) context variable (sphere roller, walkers, the single animal): at the first tick
of each decision window, the observation the Observe phase wrote gives the window's action, as the trainer's
would in training. The policy itself is the world's [PolicyHandle](../components/PolicyHandle.md), injected by
[ModelWatcher](ModelWatcher.md), by `TrainHost` (mode=eval) or by a test. Only in binaries linked with
`dynamical_onnx` (`registerPolicySystems`).

## Behaviour

**tick**: nothing without both a PolicyHandle and an AgentIO in the world's context.
- With `handle.agent` (an `AgentPolicy`: the agent has sensors or discrete branches): `run(io, deterministic)` feeds
  `obs_0` (observation) and `obs_1...` (sensors, buffers as rows), all-ones action masks, and writes the
  continuous actions and the discrete choices (clamped to their branch). A `PolicyError` (layout mismatch,
  ONNX Runtime error) is logged and the agent policy dropped from the handle.
- Else with `handle.runner` (a `PolicyRunner`: one vector in, continuous actions out): when the AgentIO's
  observation and action sizes are the model's, `run(observation, 1, action, deterministic)`; otherwise nothing
  (the action stays).
- `deterministic`: the mean action, else mean + noise as in training.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PolicyHandle](../components/PolicyHandle.md) | context | readwrite | tick | the runner or agent policy run; an AgentPolicy that throws is dropped (agent reset) |
| [AgentIO](../components/AgentIO.md) | context | readwrite | tick | observation (and sensors) read, action (and discrete choices) written |

## Side effects

- ONNX Runtime inference on the CPU, on the worker that steps the world.
- Logs an error and drops the AgentPolicy when its layout does not match the agent.

## Order and dependencies

- Act phase: after the previous window's Observe system (which wrote the observation), before the systems that
  apply the action (the agent's drive system of the template).
- A handle replaced between two windows (ModelWatcher loading a newer checkpoint) is used from the next decision.

## Used in scenes
<!-- generated -->

## Tests

- `SR.16` in [`tests/onnx/policy_runner.cpp`](../../../tests/onnx/policy_runner.cpp): the Policy system writes the
  policy's action and the trained sphere roller beats the controller on marching cubes; `SR.13`, `SR.14`
  (PolicyRunner).
- `SR.18` to `SR.20` in [`tests/onnx/vr_roller.cpp`](../../../tests/onnx/vr_roller.cpp); `AP.1` in
  [`tests/onnx/agent_policy.cpp`](../../../tests/onnx/agent_policy.cpp) (AgentPolicy on a buffer and a branch).
