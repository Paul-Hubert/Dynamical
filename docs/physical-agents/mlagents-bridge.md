# ML-Agents bridge

`dynamical_train` speaks ML-Agents communication protocol **1.5.0** so the unchanged Python trainer
(the fork, its configs, GAIL) treats it exactly like a Unity player build (`TrainingEnv.exe`).

Items marked *verify* were written from memory of upstream ML-Agents; confirm each against the fork's
`mlagents_envs` (Python) and `com.unity.ml-agents/Runtime/Communicator` (C#) before implementing.

## Roles

- **Python is the gRPC server**, listening on `localhost:<port>` (`base_port + worker_id`).
- **The engine is the client.** It calls the single RPC `UnityToExternalProto.Exchange(UnityMessageProto)
  returns (UnityMessageProto)` repeatedly. Each call sends the engine's output and blocks until Python
  replies with its next input.

## Protos

Vendor the fork's `protobuf-definitions/proto/mlagents_envs/communicator_objects/*.proto` into
`src/mlagents/proto/` unchanged (same package paths) and generate C++ and gRPC stubs in CMake
([architecture.md](architecture.md#cmake)). Main types:

| Message | Use |
|---|---|
| `UnityMessageProto { header, unity_output, unity_input }` | Envelope both ways. `header.status` 200 = OK, 400 = closing |
| `UnityOutputProto { rl_output, rl_initialization_output }` | Engine → Python |
| `UnityInputProto { rl_input, rl_initialization_input }` | Python → engine |
| `UnityRLInitializationOutputProto` | name, `communication_version`, `package_version`, `log_path`, `brain_parameters`, `capabilities` |
| `UnityRLInitializationInputProto` | `seed`, `communication_version`, `package_version`, `capabilities`, `num_areas` |
| `UnityRLOutputProto { map<string, ListAgentInfoProto> agentInfos; bytes side_channel }` | Per-step observations |
| `UnityRLInputProto { map<string, ListAgentActionProto> agent_actions; UnityRLInputProto.Command command; bytes side_channel }` | Per-step actions and `STEP`/`RESET`/`QUIT` |
| `AgentInfoProto` | `reward`, `done`, `max_step_reached`, `id`, `action_mask`, `observations`, `group_id`, `group_reward` |
| `ObservationProto` | `shape`, `compression_type` (NONE), `float_data.data`, `compressed_channel_mapping`, `dimension_properties`, `observation_type`, `name` |
| `AgentActionProto` | `continuous_actions`, `discrete_actions` (`vector_actions_deprecated` unused) |
| `BrainParametersProto` | `brain_name`, `is_training`, `action_spec { num_continuous_actions, num_discrete_actions, discrete_branch_sizes, action_descriptions }` |
| `UnityRLCapabilitiesProto` | feature flags (below) |

## Message sequence

```
engine                                                   python
  | Exchange( output{ rl_initialization_output } ) ----->  |  checks communication_version
  | <---------------- input{ rl_initialization_input } --- |  seed, num_areas, versions
  | Exchange( header 200, no output ) ------------------>  |  (Python's initialize() consumes this)
  | <--------------------- input{ rl_input: RESET } ------ |  sent when the trainer calls env.reset()
  |   reset all arenas                                     |
  | Exchange( output{ rl_output, rl_initialization_output{brain_parameters} } ) -> first observations
  | <----------------- input{ rl_input: STEP + actions } - |
  |   scatter, step decision_period ticks, gather          |
  | Exchange( output{ rl_output } ) -------------------->  |
  |   ...                                                  |
  | <--------- input{ rl_input: QUIT } or header 400 ----- |  exit cleanly (code 0)
```

*Verify*: upstream C# `RpcCommunicator.Initialize` performs the two initial `Exchange` calls shown
(init output, then an empty message whose reply is the first real input).

### Initialisation output

| Field | Value |
|---|---|
| `name` | `"Dynamical"` |
| `communication_version` | `"1.5.0"` |
| `package_version` | the C# package version the fork pairs with (*verify*; Python warns on mismatch, does not fail) |
| `log_path` | the `-logFile` argument, or empty |
| `capabilities` | `base_rl_capabilities`, `concatenated_png_observations`, `compressed_channel_mapping`, `hybrid_actions`, `training_analytics`, `variable_length_observation`, `multi_agent_groups` all `true` (we only ever send uncompressed float observations, so claiming these is harmless) |
| `brain_parameters` | empty here; sent lazily (next section) |

From the reply keep `seed` (→ `base_seed` for arena RNGs) and `num_areas` (→ number of arenas; treat
0 as 1). If Python's `communication_version` is not compatible (major differs, or minor differs while
major is 0), log and exit with a non-zero code.

### Brain parameters

The first `UnityOutputProto` that contains agents of a behaviour must also carry
`rl_initialization_output.brain_parameters` for that behaviour (`brain_name` = `"Walker?team=0"`,
`is_training = true`, `action_spec` from the `BehaviorSpec`). Keep a `std::set` of announced behaviours.
Python builds the observation specs from the **first** `AgentInfoProto` it sees for a behaviour, so
observation shapes, names and dimension properties must be identical for every step.

### Step output

For each behaviour: a `ListAgentInfoProto` with one `AgentInfoProto` per `AgentStep` gathered from the
arena outboxes, in arena order then agent creation order.

```cpp
void fill(const AgentStep& s, const BehaviorSpec& b, mlagents::AgentInfoProto& p) {
    p.set_id(s.id);  p.set_reward(s.reward);  p.set_done(s.done);
    p.set_max_step_reached(s.max_step_reached);
    p.set_group_id(s.group_id);  p.set_group_reward(s.group_reward);
    for (size_t i = 0; i < b.observations.size(); ++i) {
        auto* o = p.add_observations();
        const auto& spec = b.observations[i];
        for (int d : spec.shape) o->add_shape(d);
        for (int d : spec.dimension_properties) o->add_dimension_properties(d);
        o->set_compression_type(mlagents::NONE);
        o->set_observation_type(static_cast<mlagents::ObservationTypeProto>(spec.observation_type));
        o->set_name(spec.name);
        o->mutable_float_data()->mutable_data()->Add(s.obs[i].begin(), s.obs[i].end());
    }
    for (uint8_t m : s.action_mask) p.add_action_mask(m != 0);
}
```

Reuse the `UnityMessageProto` between steps (`Clear()` keeps capacity) to avoid per-step allocation.

### Action routing

Python replies with `agent_actions[behaviour]`: a list with one `AgentActionProto` for each agent that
**requested a decision** in the previous output, i.e. every agent of that behaviour whose `done` was
false, in the same order. Done agents (terminal-only entries) get no action. *Verify* against C#
`m_OrderedAgentsRequestingDecisions`.

So the bridge keeps, per behaviour, the vector of `(arena, entity)` of non-done agents it last sent, and
writes action *i* into the `ActionBuffer` of entry *i*, setting `fresh = true`. Size mismatch → log
both sizes and exit (it means the protocol is out of sync).

Continuous actions arrive already clipped to `[-1, 1]` by the trainer (*verify*: upstream Python clips
with `clamp(-3,3)/3` when `action_clipping` is on in the fork). `ActionLayout::apply` may assume
`[-1, 1]` but must still clamp defensively.

### Commands

| Command | Engine action |
|---|---|
| `STEP` | scatter actions → `step_parallel` → gather → send |
| `RESET` | reset every agent in every arena (applying env params received in the same message first) → gather without stepping → send |
| `QUIT` / header 400 / channel closed | flush logs, exit 0 |

## Side channels

`side_channel` bytes in both directions are a concatenation of messages:

```
[16 bytes channel UUID][int32 little-endian payload length][payload]
```

The UUID bytes are in **.NET `Guid.ToByteArray()` order** (Python uses `uuid.UUID(bytes_le=...)`): the
first three fields are little-endian, the last eight bytes as written. Payload primitives are
little-endian: `int32`, `float32`, `bool` (1 byte), and strings as `int32` byte length + UTF-8 bytes
(*verify* in `mlagents_envs/side_channel/{incoming,outgoing}_message.py`).

| Channel | UUID (*verify*) | Direction | Handling |
|---|---|---|---|
| Engine configuration | `e951342c-4f7e-11ea-b238-784f4387d1f7` | Py → engine | Parse and ignore (`time_scale`, `target_frame_rate`, `quality_level`, `screen_resolution`, `capture_frame_rate`); log once |
| Environment parameters | `534c891e-810f-11ea-a9d0-822485860400` | Py → engine | `string key, int32 type` then FLOAT=0: `float value`; SAMPLER=1: `int32 seed, int32 sampler_type` then UNIFORM=0 `min,max` / GAUSSIAN=1 `mean,std` / MULTIRANGEUNIFORM=2 `int32 n, n floats` → `EnvParams` |
| Stats | `a1d8f7b7-cec8-50f9-b78b-d3e165a78520` | engine → Py | `string key, float value, int32 aggregation` (AVERAGE=0, MOST_RECENT=1, SUM=2, HISTOGRAM=3) |
| Training analytics | (fork's) | engine → Py | not sent |
| Unknown UUID | — | Py → engine | skip by length, warn once per UUID |

Stats entries from all arena outboxes go into the side channel of the next output message.

## Launch contract

The trainer starts the executable given as `env_path` (on Windows it appends `.exe`; on Linux it looks
for `<name>.x86_64` or `<name>`) with Unity-style arguments. `dynamical_train` must accept and ignore
what it does not use:

| Argument | Meaning | Engine |
|---|---|---|
| `--mlagents-port <n>` | gRPC port | required |
| `-logFile <path>` | log file | redirect `dy::log` there |
| `-nographics`, `-batchmode` | headless | always headless; ignore |
| `--env-args ...` contents | passed through from the trainer YAML / CLI | our own flags: `--scenario`, `--threads`, `--seed`, `--mode`, `--model`, `--tracks`, `--out` |
| anything else starting with `-` | Unity flags | ignore with a single warning |

For the fork's `start-run` / `stop-run` tooling: point its environment path at `dynamical_train`
(or a small wrapper named like the old `TrainingEnv` so configs stay unmodified) and put
`--scenario scenarios/<line>.scenario.json` in the run's `env_args`. Model export at `stop-run` is done
by Python and needs nothing from the engine.

## Failure handling

| Situation | Behaviour |
|---|---|
| Python not listening yet | Retry connect with backoff for up to 60 s (Python's own default wait), then exit 1 |
| `Exchange` fails mid-run | Log status code and message, exit 1 (Python restarts the env worker) |
| NaN/Inf in an observation or reward | Replace with 0, log agent id and segment once per episode, increment a `Stats/NaNCount` stat; terminate the episode (Jolt explosion) |
| Exception in a system | Caught per arena task; the arena is rebuilt and its agents report `done` with `max_step_reached = true` |

## Agent ids

`AgentInfoProto.id` = `AgentId::value`, a process-unique `int32` assigned when the slot is created and
kept across episodes. Never derive it from `entt::entity` (EnTT recycles entity ids with a new version).

## M0: null environment

`--mode null-env --obs-size N --act-size M` creates `num_areas × agents_per_arena` agent slots with no
physics: observations are random normal, reward 0, episodes truncate every `max_episode_steps`. It
exercises the whole protocol and gives the M0 gate: trainer steps/s at 6 and 20 envs. If throughput
stays near ~424 steps/s regardless of the engine, the trainer is the bottleneck.
