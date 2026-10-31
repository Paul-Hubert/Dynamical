# ML-Agents bridge

**`dynamical_train` must be indistinguishable from the Unity `TrainingEnv.exe`** to the fork's
`mlagents-learn`, `start-run` and `stop-run`. UP §3.9 is the requirement list; this doc is the design.
Wire details marked *verify* must be checked against the fork's
`ml-agents-envs/mlagents_envs/communicator_objects/*.proto`, `rpc_communicator.py`, the side-channel
classes and the C# `RpcCommunicator` before coding.

## Launch

The trainer starts one process per env:

```
TrainingEnv.exe -nographics -batchmode --mlagents-port <base+i> -logFile <run>/Player-<i>.log
                [-envParamsOut=<path>] [-gitSha=<sha>] [-envParams=<path>] [-set:<name>=<value> ...]
                [env_args from the config]
```

- `train_args.cpp` parses these Unity-style flags. `-nographics` and `-batchmode` are accepted and
  ignored, since the process is always headless.
- Everything else uses the engine's own `key=value` (`scene=walker_train var.num_worlds=25
  threads=4`). Configs pass those through `env_args`.
- The default scene is `walker_train`, so an unmodified config needs no extra arguments.
- **Logging.** All logging goes to `-logFile` (the health check greps it) and to stdout.
- **Executable name.** The trainer globs `<path>.exe`, so the build names the target `TrainingEnv`
  ([architecture.md](architecture.md#targets)) and `start-run` points `--env` at it.

## Roles and protocol

- Python is the gRPC **server** on `localhost:<port>`.
- The engine is the **client**. It must connect within 60 s and repeatedly calls
  `UnityToExternalProto.Exchange(UnityMessageProto) → UnityMessageProto`.
- Communicator version `1.5.0`, package version `4.0.0` (UP §3.9), and the RL capability flags.

### Sequence (*verify* the initial double exchange against the C# communicator)

```
engine                                                          python
Exchange(output{ rl_initialization_output: name, versions, capabilities }) ->
                                           <- input{ rl_initialization_input: seed, num_areas, versions }
Exchange(header 200) ->
                                           <- input{ rl_input: RESET }
reset all worlds, observe without stepping
Exchange(output{ rl_output: agentInfos, rl_initialization_output{ brain_parameters } }) ->
                                           <- input{ rl_input: STEP + agent_actions }
scatter, step one window, gather
Exchange(output{ rl_output }) -> ...
                                           <- QUIT, or header 400        exit 0
```

- **Seed.** `base_seed = seed + worker_id`. Worker id = port − base port. World `w` seeds with
  `hash(base_seed, w)`.
- **`num_areas` is ignored.** The scene's `num_worlds` decides the agent count, as Unity's PrefabGrid
  did. Log both.
- **Brain parameters** are sent once per behaviour **together with** its first `AgentInfo`s, because
  the trainer infers the observation specs from them:
  - `brain_name` = the Unity behaviour name (plus `?team=N` for self-play);
  - `is_training`;
  - `action_spec { num_continuous_actions = layout size }`.
- **RESET arrives mid-run** on every curriculum lesson change. Reset every agent, emit fresh
  observations with reward 0 and not done, and keep the `AgentId`s.

### AgentInfo

Per agent, per window:
- `id` (stable `AgentId`);
- `reward` (since the last decision);
- `done`, with `max_step_reached` for truncation;
- `group_id`, `group_reward`;
- one `ObservationProto` per sensor: `shape`, `compression_type` NONE, `float_data`, `name`,
  `dimension_properties`, `observation_type`. Sensors are **sorted by name**, and the names and shapes
  never change during a run.

### Action routing

- Python sends actions only for agents that requested a decision and are not done.
- Actions arrive in the order of the previous `AgentInfo` list for each behaviour (*verify*). The
  bridge keeps that order per behaviour and maps actions back to `AgentId`s.
- Agents that were done get no action; they are already reset and their first observation was in
  this output.
- **NaN or Inf** in an observation: log it, zero the observation, end the episode as interrupted, and
  count it in the `Bridge/NaN` stat.

## Side channels

The payload is a sequence of `[16-byte UUID][int32 length][bytes]` messages (*verify* UUID byte
order: Python `uuid.bytes_le`).

| Channel | Direction | Handling |
|---|---|---|
| Environment parameters | in | Float and Sampler messages update `EnvParams` |
| Engine configuration | in | Accepted, logged once, ignored (`time_scale` means nothing here) |
| Stats | out | `{key, value, aggregation 0..3}`, with Unity's names (`Root/*`, `Head/*`, `Death/*`, `Win/*`, `Bot Joint/*`, `EnergyProvider/*`, `DeVita/*`, `Collision/*`), only while training |

## Environment parameters

Precedence, highest first (UP §3.9):
1. `-set:<name>=<value>`;
2. the live value from the trainer;
3. the `-envParams=<file>` replay;
4. the params-file default.

- Values apply at initialisation and at **every episode start**, never mid-episode.
- Sampler parameters are sampled with the world RNG per episode.
- **An unknown name logs a warning once.** Today Unity ignores it silently.
- Layout-changing parameters are rejected with an error ([assets.md](assets.md#agent-parameters)).
- **`envparams.txt`** is written to `-envParamsOut=` in Unity's format, **including `sha=`**: the git
  sha of the engine build, plus the body file and params file hashes.

## Process behaviour

- `stop-run` sends CTRL_C to the console group. Install a console control handler that sets a flag.
  The main loop then finishes the current exchange, returns, and exits 0 quietly, so the trainer can
  export the final ONNX.
- A lost connection (gRPC error, or header 400): exit 0 if after QUIT, non-zero otherwise, logging
  the reason.

## Null environment (M0)

`mode=null`:
- the real handshake and step loop;
- 25 agents with a fixed observation (138 × 2 zeros for the noToes layout, with Unity's sensor name) and zero rewards;
- episodes of `maxFrames / decision_period` decisions;
- no worlds, no physics.

It measures trainer steps/s at 6 and 20 envs. The result goes in `benchmarks.md`.
