# ML-Agents bridge

**`dynamical_train` must be indistinguishable from the Unity `TrainingEnv.exe`** to the fork's
`mlagents-learn`, `start-run` and `stop-run`. The Unity-side contract is in [unity-analysis.md](unity-analysis.md) §9; this doc is the design.
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

- **Verified against the fork** (`ml-agents-envs/mlagents_envs/environment.py:269-284`): the worker
  arguments are `-nographics -batchmode`, then `--mlagents-port <port>`, `-logFile <path>`, then the
  config's `env_args`. `start-run` appends `-envParamsOut=… -gitSha=…` through `env_args` (UA §9.4).
- `train_args.cpp` parses these Unity-style flags. `-nographics` and `-batchmode` are accepted and
  ignored, since the process is always headless.
- **Working directory.** `dynamical_train` never changes the working directory, because the trainer
  passes relative paths (`-envParamsOut=results/<run>/…`). Resources are resolved from the executable's
  directory (`GetModuleFileNameW`). The deployed layout is `TrainingEnv.exe` plus `resources/scenes`,
  `resources/prefabs` and `resources/agents` next to it.
- **No config file.** It never constructs `Settings` with load or save, so N processes do not race on
  `config.6.json` ([architecture.md](architecture.md#run-arguments)).
- Everything else uses the engine's own `key=value` (`scene=walker_train var.num_worlds=25
  threads=4`). Configs pass those through `env_args`.
- The default scene is `walker_train`, so an unmodified config needs no extra arguments.
- **Logging.** All logging goes to `-logFile` (the health check greps it) and to stdout; see "Process behaviour".
- **Executable name.** The trainer globs `<path>.exe`, so the build names the target `TrainingEnv`
  ([architecture.md](architecture.md#targets)) and `start-run` points `--env` at it.

## Roles and protocol

- Python is the gRPC **server** on `localhost:<port>`.
- The engine is the **client**. It must connect within 60 s and repeatedly calls
  `UnityToExternalProto.Exchange(UnityMessageProto) → UnityMessageProto`.
- Communicator version `1.5.0`, package version `4.0.0` (verified: `com.unity.ml-agents/Runtime/Academy.cs:104`,
  `:110`; `package.json:4`) and the RL capability flags, all true as the C# defaults
  (`Communicator/UnityRLCapabilities.cs:56-63`).

### Sequence (verified)

Verified against the fork (`ml-agents` @ `cc376888`): the C# communicator sends the initialisation output, then an
empty message (`Communicator/RpcCommunicator.cs:235-236`); Python's `initialize()` receives the first, answers it
with the initialisation input, then receives the second without answering (`rpc_communicator.py`, `initialize`), so
the second call returns the first command when the trainer resets. `close()` answers the pending call with header
400.

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

- **Seed.** The trainer already sends `seed + worker_id` (`learn.py:190`, `env_seed = seed + worker_id`),
  so `base_seed = rl_initialization_input.seed`. World `w` seeds with `hash(base_seed, w)`.
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
- Actions arrive in the order of the previous `AgentInfo` list for each behaviour, **skipping the done agents**
  (verified: `RpcCommunicator.cs:361-368` records only non-done agents, in `PutObservations` order;
  `:419-428` maps action `i` to the `i`-th of them). The bridge keeps that order per behaviour and maps actions
  back to `AgentId`s.
- Agents that were done get no action. Their terminal step is in this output, and the first observation
  of their new episode is in the **next** output, under the same `AgentId`. This matches Unity, where the
  fork assigns `episodeId` once per agent (`Agent.cs:489`).
- **NaN or Inf** in an observation: log it, zero the observation, end the episode as interrupted, and
  count it in the `Bridge/NaN` stat (an extra; see agent-runtime.md "Stats").

## Side channels

The payload is a sequence of `[16-byte UUID][int32 length][bytes]` messages, little endian. The UUID is in
.NET `Guid.ToByteArray` order, which is Python's `uuid.bytes_le` (verified: `SideChannels/SideChannelManager.cs:139-141`,
`side_channel_manager.py:22-24`, `:58-59`). Channel ids: environment parameters `534c891e-810f-11ea-a9d0-822485860400`,
engine configuration `e951342c-4f7e-11ea-b238-784f4387d1f7`, stats `a1d8f7b7-cec8-50f9-b78b-d3e165a78520`.
Strings are an int32 length plus ASCII bytes; a stat is key, float value, int32 aggregation
(`StatsSideChannel.cs:30-32`). P3.2 checks the engine's codec byte for byte against blobs written by the fork's
Python classes (`tools/capture_sidechannels.py`).

| Channel | Direction | Handling |
|---|---|---|
| Environment parameters | in | Float and Sampler messages update `EnvParams` |
| Engine configuration | in | Accepted, logged once, ignored (`time_scale` means nothing here) |
| Stats | out | `{key, value, aggregation 0..3}`, with Unity's names (`Root/*`, `Head/*`, `Death/*`, `Win/*`, `Bot Joint/*`, `EnergyProvider/*`, `DeVita/*`, `Collision/*`), only while training |

## Environment parameters

Precedence, highest first (UA §9.3):
1. `-set:<name>=<value>`;
2. the live value from the trainer;
3. the `-envParams=<file>` replay;
4. the params-file default.

- Values apply at initialisation and at **every episode start**, never mid-episode. The exception is
  `physicsRate`, which applies once at start (UA §9.3).
- Sampler parameters are sampled with the world RNG per episode. Bools are `value > 0`.
- A missing or malformed `-envParams` file is fatal (exit 1), as in Unity.
- **An unknown name logs a warning once.** Unity ignores it silently.
- **Layout-defining parameters** (`springControl`, `angularVelocityControl`, `jointSpace`, `relativeVelocity`,
  `normalizedVelocity`, `freezeObs` and the `*Obs` flags; one list in [assets.md](assets.md#agent-parameters)):
  - accepted silently when equal to the params file;
  - **fatal (exit 1, logged) when different**, because the layout cannot change at runtime.

  Unity configs that resend the scene value still load.

**Accepted names** (union from UA §7, §4.1, §3.6, §9.3):

| Group | Names |
|---|---|
| Body and drive | `rotationSpring`, `rotationDamper`, `maxTorque`, `torqueFactor`, `maxAngularVelocity`, `angularVelocityFactor`, `maxTargetAngularVelocity`, `totalMass`, `physicsRate` |
| Task | `targetSpeed`, `continuousSpeed`, `targetSpeedMin`, `targetSpeedMax`, `randomize`, `randomTarget`, `randomSpeed`, `randomInit`, `randomInitChance`, `arcFrames`, `winDistance` |
| Disturbances | `pushing`, `pushChance`, `pushForce`, `pulling`, `pullChance`, `pullForce` |
| Rewards and termination | `rootReward`, `headSpringReward`, `headDamperReward`, `winReward`, `deathPenalty`, `dyingHeightFactor`, `maxDyingFrames`, `lifeReward`, `dyingReward`, `dieOnFall`, `resetOnAlive`, `collisionPenalty`, `rewardFactor`, `normalizeSpeedDiff` |
| Energy | `energyBonusWeight`, `energyScale`, `energySpeedEdge`, `energyMinSpeedFloor`, `energyVelocitySmoothingTau`, `positiveEnergyMultiplier`, `negativeEnergyMultiplier`, `energySource` (0–2 mapped to 3 with a warning), `energyClosure`, `energyChargeResidual` and `energyFullGrf` (accepted, no-ops on Jolt, warned once), `statsSettleSeconds`, `useTorqueNotEnergy`, `energyLimitEpsDeg`, `energyRewardStyle`, `energyNormDivisor`, `energyNormOffset`, `energyMinSpeed`, `energyMaxSpeed`, `etaPositiveWork`, `etaNegativeWork` |
| Slope (M7) | `slopeAngle`, `slopeRandom`, `slopeIncludeLevel`, `slopeSign`, `slopeCompensateSpeed`, `slopeFootReach`, `slopeLiftMargin` |
| Misc | `selfColliding` (0 accepted; 1 is fatal: self-collision is not implemented), `freezeJointActions`, `pinFrozenJointDrive` |
| Inert, accepted silently | `depth`, `energyPenalty`, `legacy*` |

**`envparams.txt`** is written to `-envParamsOut=` in Unity's format (UA §9.3):
- rewritten atomically on every change;
- header `# run=<dir> behavior=<name> written=<steps>`, then `name value` lines sorted ordinally, with
  round-trippable floats;
- **plus** `sha=<engine git sha>`, `body=<sha256>`, `params=<sha256>` and `layout=<version>`. Unity
  parses `-gitSha` but never writes it.

## Process behaviour

- **CTRL_C.** `stop-run` sends CTRL_C to the console group.
  - `SetConsoleCtrlHandler` returns `TRUE` for `CTRL_C_EVENT`, `CTRL_BREAK_EVENT` and
    `CTRL_CLOSE_EVENT`, and sets a flag.
  - Each gRPC call is issued once and awaited in 1 s slices (async API), so the loop sees the flag within a
    second. A call is never re-sent after a timeout: the trainer may already hold the message, and a duplicate
    would break the lockstep.
  - **Once the flag is set, the process exits 0 regardless of the connection state.** The trainer dies
    at the same time.
- **A lost connection without the flag:** exit 0 after QUIT or header 400; otherwise exit 2 with the
  reason logged.
- **Logging.** `dy::log` gains a file sink, opened from `-logFile` before any other output and flushed
  per line, because the health check greps it live. stdout remains.

## Null environment (M0)

`mode=null`:
- the real handshake and step loop;
- 25 agents with a fixed observation (138 × 2 zeros for the noToes layout) and zero rewards. The sensor is
  Unity's `StackingSensor` around the agent's `VectorSensor` (`Agent.cs:1022-1025`), so its name is
  `StackingSensor_size2_VectorSensor_size138` (`StackingSensor.cs:62`, `VectorSensor.cs:28`), its shape `[276]`,
  dimension properties `[0]` and observation type `DEFAULT` (`ObservationSpec.cs:62-69`, `GrpcExtensions.cs:430-457`);
- episodes of 1000 decisions (2000 ticks / decision period 2);
- no worlds, no physics.

It measures trainer steps/s at 6 and 20 envs. The result goes in `benchmarks.md`.
