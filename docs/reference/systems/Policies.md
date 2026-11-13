# Policies

`AgentPoliciesSys` in [`src/onnx/agent_policy.cpp`](../../../src/onnx/agent_policy.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, `Policies@act` (replacing `TrainerActions@act`) · Parameters: yes

Inference for worlds whose agents are components (pursuit, the sandbox): every [AgentIO](../components/AgentIO.md)
component that asked for a decision gets the action of its behaviour's exported policy, as the training host would
give it the trainer's, and its request and stats are consumed. A behaviour without a model is left alone, for
another system (the sandbox's scripted minds) or nobody to answer. Only in binaries linked with `dynamical_onnx`.

## Behaviour

**tick**: for every entity with an AgentIO:
- done: `requested` cleared, nothing else (a done agent gets no action);
- not requested: skipped;
- otherwise the behaviour's policy (`policyOf`, looked up once per behaviour and remembered, also when there is
  none): only behaviours starting with `prefix` (if set); the file is `behaviors[<behavior>]`, else
  `findBehaviorModel(models, behavior)` (`<models>/<behavior>.onnx`, the trainer's export at the end of a run, else
  the largest step of `<models>/<behavior>/<behavior>-<step>.onnx`). Sessions come from a process-wide cache keyed
  by canonical path and write time, so the worlds of a scene share them.
- `AgentPolicy::run(io, deterministic)`: observation and sensors in, continuous actions and discrete choices out;
  then `requested = false` and stats cleared. A `PolicyError` (the model's layout is not the agent's) is logged and
  the behaviour is marked as having no model from then on.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `models` | string | `""` | a run folder of the trainer (path as given, relative to the working directory) |
| `behaviors` | object | `{}` | `{ "<behavior>": "<file.onnx>" }`, looked at before `models` |
| `prefix` | string | `""` | only the behaviours whose name starts with it (`Animal_`: the walkers alone) |
| `deterministic` | bool | true | mean actions, else sampled |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | component | readwrite | tick | behaviour, observation and sensors read; action and discrete choices written; `requested` and stats cleared once answered, `requested` cleared on a done agent |

## Side effects

- Looks up <models>/<behavior>.onnx, else the largest step of <models>/<behavior>/<behavior>-<step>.onnx, once per behaviour.
- ONNX Runtime sessions shared by every world of the process (static cache under a mutex, keyed by canonical path and write time).
- Logs info per behaviour loaded, debug when a behaviour has no model, error when a model is refused (the behaviour is then left to others).

## Order and dependencies

- Act phase, in place of `TrainerActions@act`: after the previous window's Observe systems (which set `requested`)
  and before the systems that read the actions ([Sandbox](Sandbox.md), the pursuit's agents).
- In the sandbox it runs before Sandbox's own `heuristic` minds, which answer only what it left requested.

## Used in scenes

<!-- generated -->
- [`pursuit_eval.scene.json`](../../../resources/scenes/pursuit_eval.scene.json): `fixed@act`
- [`sandbox_eval.scene.json`](../../../resources/scenes/sandbox_eval.scene.json): `fixed@act`
- [`sandbox_view.scene.json`](../../../resources/scenes/sandbox_view.scene.json): `fixed@act`

## Tests

- `AP.1` to `AP.3` in [`tests/onnx/agent_policy.cpp`](../../../tests/onnx/agent_policy.cpp): a model with a vector,
  a buffer and a branch; a behaviour's model from a run folder; trained chasers catch runners in `pursuit_eval`,
  chasers without a model do not.
