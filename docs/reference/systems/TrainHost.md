# TrainHost

> **Kind:** service · **Registry name:** — · **Scope:** n/a · **Lane:** main loop of `dynamical_train` (`TrainHost::run` from `src/train_main.cpp`) · **Needs renderer:** no
> **Source:** [`src/train/train_host.h`](../../../src/train/train_host.h), [`src/train/train_host.cpp`](../../../src/train/train_host.cpp)

## Purpose
The loop of `dynamical_train` (TrainingEnv.exe): there is no frame loop, the worlds step as fast as the CPU allows. It loads environment parameters, then runs one of three modes: `bench` (throughput), `null` (the ML-Agents contract with a fixed `NullEnvironment`) or `train` (the scene's agent worlds through `WorldsEnvironment`). In the two trainer modes it performs the ML-Agents 1.5.0 handshake over gRPC and serves RESET / STEP / QUIT until the trainer quits, closes, the connection fails or a stop signal arrives. Its only registry access is reading world 0's `AgentIO` behaviour name.

## Lifecycle
- **Construction / params:** `TrainHost(const RunArgs& args, const TrainArgs& train, ScenarioRunner& runner)`; references are kept. Fields: `EnvParamStore env_params`, `std::atomic<int64_t> base_seed{-1}`, `behavior = "HeidelBotBehavior"` (envparams header), last written values.
- **preinit:** None.
- **init:** None.
- **tick(dt):** `run()`:
  1. `mode = args.mode` or `"train"` when empty. For `train`, when `WorldsEnvironment::check(runner)` passes, `behavior` = world 0's `AgentIO.behavior`.
  2. `loadEnvParams()`: `-envParams=<file>` loaded with `EnvParamStore::loadFile` (missing or malformed: log error, return 1); each `-set:<name>=<value>` applied with `setOverride` (non-number: return 1) and logged; then `writeEnvParams(0)`.
  3. Dispatch: `bench` / `null` / `train`; any other mode logs "not available yet" and returns 1.
  - `bench()`: `ticks = args.ticks > 0 ? args.ticks : 600`; `threads = args.threads > 0 ? args.threads : max(1, min(worlds, hardware_concurrency))`; a local `tf::Executor(threads)`; `runner.step(ticks, &executor)` timed with `steady_clock`; logs ticks/s and world-ticks/s; returns 0.
  - `null()`: needs `train.port > 0` (else 1). `NullEnvironment::Config` with `agents` and `episode_decisions` taken from the resolved scene's `variables.<name>.default` when present (defaults 25 and 1000); `serve` with a factory that logs and builds the `NullEnvironment` (seed ignored).
  - `agents()`: needs `train.port > 0` and `WorldsEnvironment::check(runner)` empty (else logs and returns 1); `behavior` from world 0's `AgentIO`; `threads = args.threads > 0 ? args.threads : 1`. `serve` with a factory that calls `runner.restart(seed)` and builds `WorldsEnvironment(runner, threads)`, logging behaviour, observation/action sizes and decision period.
  - `serve(connected)`: creates `MlAgentsClient(train.port, stopRequested)`; builds the initialisation output (name `Dynamical`, communication `1.5.0`, package `4.0.0`, capabilities baseRL, concatenated PNG observations, compressed channel mapping, hybrid actions, training analytics, variable-length observation, multi-agent groups all true); `client.initialize` (60 s connect timeout). `Stopped` → log and 0; `Closed` → 0; other failure → log and 2. Stores `base_seed = init_in.seed()` (logs versions, seed, `num_areas` ignored), calls `connected(seed)`. Loop, per received input:
     1. `stopRequested()` → log the signal, return 0.
     2. Parse `rl_input.side_channel`: a truncated payload is logged and ignored; environment-parameter `Float` messages → `env_params.setLive(key, value)`; `Sampler` messages logged only; the first engine-configuration message is logged as ignored; other channels ignored.
     3. `writeEnvParams(steps)` (writes only when the resolved values changed or nothing was written yet).
     4. `QUIT` → return 0; `RESET` → `env->reset()`; any other command → `env->step(rl, error)` (false → log, return 2), `steps++`.
     5. `env->write(output)`, `client.exchange(output, input)`: `Stopped` → 0, `Closed` (header != 200) → 0, failure → 2.
- **finish:** None.

## Expected behavior
- Exit codes: 0 on QUIT, trainer close (header 400) or stop signal; 1 on bad arguments, missing port, unknown mode, an untrainable scene or an env-params error; 2 when the connection fails or a STEP is malformed (`tests/protocol/null_env.cpp`, `tests/protocol/sphere_env.cpp`, `tests/protocol/ctrl_c.cpp`).
- `baseSeed()` is -1 until the handshake, then the trainer's seed.
- Env-param precedence in the written file: `-set:` > live side-channel value > `-envParams=` file (`EnvParamStore::resolved`).
- `envparams.txt` (`-envParamsOut=`) is written at start with `written=0`, then again only when values change, header `behavior=<behavior>` and `sha=<DYNAMICAL_GIT_SHA>`; a failed write logs a warning and is retried on the next change.
- mode=train on a scene without agent worlds exits 1 before connecting (SR.10).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | world | read | run | world 0's `behavior`, in `run()` (mode train) and `agents()` |

## Side effects
- gRPC client to `localhost:<port>` (blocking calls awaited in 1 s slices).
- Reads `-envParams=` file; writes `-envParamsOut=` file (temporary file then rename).
- Logs through `dy::log` (also copied to `-logFile` when set).
- `bench` creates a `tf::Executor`; `agents` restarts the runner's worlds with the trainer's seed (world RNGs reseeded).
- Polls the process stop flag (`process_control`).

## Ordering and dependencies
- Constructed after the `ScenarioRunner` (scene loaded and checked with base seed 0); the worlds are rebuilt with the trainer's seed only after the handshake.
- Single-threaded; worlds may step on the `WorldsEnvironment` executor inside `env->step`.

## Related
[WorldsEnvironment](WorldsEnvironment.md), [ScenarioRunner](ScenarioRunner.md), [folder `src/train/`](../folders/src-train.md), [folder `src/mlagents/`](../folders/src-mlagents.md), `docs/physical-agents/mlagents-bridge.md`.
