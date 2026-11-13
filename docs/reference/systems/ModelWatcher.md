# ModelWatcher

`ModelWatcherSys` in [`src/onnx/policy_systems.cpp`](../../../src/onnx/policy_systems.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, before `WorldStepper` · Parameters: yes

The viewer's policy loader: it gives every world of the scene a [PolicyHandle](../components/PolicyHandle.md) for
its [Policy](Policy.md) system, from a fixed `.onnx` file or from the newest checkpoint of a training run, and
swaps in newer checkpoints while the trainer writes them, so a viewer can watch a run learn. What each world runs
is published in the host's [PolicySources](../components/PolicySources.md). Only in binaries linked with
`dynamical_onnx`.

## Behaviour

**constructor**: reads the parameters, emplaces an empty PolicySources in the host registry.

**tick, first call** (`first`: host systems init before the worlds configure their agents, so nothing is loaded in
init): one PolicySource per world of the [Worlds](../components/Worlds.md) context, its spec `models["<index>"]`
or else `model`. Worlds with the same spec share a feed (one session). For each feed:
- a plain file: loaded;
- `latest:<dir>`: the `<name>-<step>.onnx` of the largest step in `dir`; with a `*` in one component of `dir`,
  the newest checkpoint of the matching directory whose newest checkpoint is the most recent; when there is none,
  the fallback after `|` if given, else an error on every world of the feed and a warning.

`load`: the first world of the feed must have an AgentIO context with observations (else an error "has no agent");
`makePolicyHandle(file, io, deterministic)` makes a `PolicyRunner` (vector observation, continuous actions) or a
shared `AgentPolicy` (sensors or branches); it is `insert_or_assign`ed into every world's registry of the feed,
their PolicySource gets the file and step and loses its error; an info line is logged. A `PolicyError` (layout,
missing file) is written to the worlds' PolicySource and logged as an error.

**tick, afterwards**: only with `latest:` feeds, never in fixed-clock runs (`RunArgs::deterministic()`), every
`poll` seconds of wall clock: the newest checkpoint of each feed; a file other than the loaded one becomes a
candidate, and is loaded when the next poll sees the same file with the same size and write time (the trainer may
still be writing it).

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `model` | string | `""` | spec of every world: `<file.onnx>` or `latest:<dir>[|<fallback.onnx>]`; empty: no policy |
| `models` | object | `{}` | `{ "<world index>": <spec> }`, overrides per world |
| `poll` | float | 2.0 | seconds between two looks at the `latest:` folders |
| `deterministic` | bool | true | mean actions (false: sampled as in training) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PolicySources](../components/PolicySources.md) | context | write+create | constructor | emplaced empty in the host registry |
| [PolicySources](../components/PolicySources.md) | context | readwrite | tick (first, load) | one PolicySource per world: spec, loaded file, step, error |
| [Worlds](../components/Worlds.md) | context | read | tick (first, load) | how many worlds and their registries |
| [RunArgs](../components/RunArgs.md) | context | read | tick | no polling in fixed-clock runs (deterministic()) |
| [AgentIO](../components/AgentIO.md) | context | read | tick (load) | the layout of the first world of a feed (observation size, sensors, branches) picks PolicyRunner or AgentPolicy |
| [PolicyHandle](../components/PolicyHandle.md) | context | write+create | tick (load) | insert_or_assign into the registry of every world of the feed: the Policy system there runs it from its next decision |

## Side effects

- Reads the file system: lists <dir> (or the directories matching a '*' component) for <behavior>-<step>.onnx every `poll` seconds of wall clock; a new checkpoint is loaded once two polls see the same size and write time.
- Creates ONNX Runtime sessions (PolicyRunner, or AgentPolicy through a process-wide cache keyed by path and write time).
- Logs info on every load, a warning when a folder has no checkpoint, an error when a model is refused.

## Order and dependencies

- The worlds must exist (scene `worlds`) and their agents be configured by their own systems' init before its first
  tick; put it before `WorldStepper` in `host.pre` so the handles are there before the worlds step.
- The worlds need the [Policy](Policy.md) system in their Act phase for the handle to be used.

## Used in scenes
<!-- generated -->

## Tests

- `SR.15` in [`tests/onnx/policy_runner.cpp`](../../../tests/onnx/policy_runner.cpp): `latest` picks the largest
  step, and across runs the most recent run.
- `SR.18` in [`tests/onnx/vr_roller.cpp`](../../../tests/onnx/vr_roller.cpp): the shipped policy through the scene's
  ModelWatcher.
