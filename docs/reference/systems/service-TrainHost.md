# TrainHost

`TrainHost` in [`src/train/train_host.cpp`](../../../src/train/train_host.cpp) (with
[`animal_host.cpp`](../../../src/train/animal_host.cpp) and [`sandbox_host.cpp`](../../../src/train/sandbox_host.cpp))
· Scope: World (the worlds' registries, from the training process) · Needs renderer: no · Lanes: none (the loop of
`dynamical_train` / `TrainingEnv.exe`, [`src/train_main.cpp`](../../../src/train_main.cpp)) · Parameters: run
arguments and scene variables

The headless training host ([simulation](../../physical-agents/simulation.md),
[ML-Agents bridge](../../physical-agents/mlagents-bridge.md)): no frame loop, the scene's worlds step as fast as the
CPU allows. It runs one mode, talks to the trainer over gRPC where the mode needs it, and reaches into the worlds'
context variables to seed them (environment parameters, policies) and to read their results.

## Behaviour

**run()**: mode `args.mode` (default `train`). For mode=train on a scene that passes
`WorldsEnvironment::check`, the envparams.txt behaviour is the first behaviour by name. Then `loadEnvParams`
(`-envParams=<file>`: Unity's envparams.txt, fatal when missing or malformed; `-set:<name>=<value>` overrides;
envparams.txt written once), and the mode:

- **bench**: every world steps `ticks` (600) ticks on `threads` workers (default: min(worlds, hardware threads));
  ticks/s logged.
- **null**: the trainer contract without worlds (`NullEnvironment`: N agents of one behaviour, zero observations,
  episodes truncated after `episode_decisions`; scene variables `agents`, `episode_decisions`).
- **train**: `serve` with a [WorldsEnvironment](service-WorldsEnvironment.md): when the trainer connects, the worlds
  are restarted with the trainer's seed (`ScenarioRunner::restart`), the environment parameters pushed, the
  environment made on `threads` workers (default 1).
- **record-demo**: one world; plays it (one warm-up window), and for every recordable window writes the world
  AgentIO's observation and sensors with zero actions as an `AgentInfoActionPairProto`, until `decisions` (3000)
  decisions are reached at the end of an episode; then the `.demo` file at `var.out` (metadata in a 33-byte header,
  brain parameters `<behavior>?team=0`, the records) and, for animal worlds, the demo checks (`writeAnimalChecks`)
  and a replay (`var.replay`). Refused when the animal's walk is not ready ([AnimalWalkState](../components/AnimalWalkState.md)).
- **eval**: sandbox worlds (a [Sandbox](../components/Sandbox.md) in world 0): `evalSandbox`, `var.seconds` (300)
  of what the scene's policies and scripted minds do, a summary per species (alive, born, brought in, deaths by
  cause, kills, falls, energy, health, lifetime, work, speed), a series every 10 s, contacts and blows, grass
  standing; `var.replay`: world 0's animals ten times a second (`.sandbox.bin` / `.json`) and the grass at the start
  and the end (`.grass.bin`). Animal worlds (an [AnimalBot](../components/AnimalBot.md) context): `evalAnimals`,
  `var.model` through `makePolicyHandle` (or zero actions), `var.episodes` (100) episodes, falls, speeds, heading
  error, by idle / walk / run, a replay of `replay_seconds`. Otherwise the walker's `eval`: `var.model` as a
  PolicyRunner, `var.episodes`, with `var.legs` CSV, `var.cot_out`, `var.replay` track, energy books. The walker and animal
  evals set `reset_requested` and run the Observe phase first; every eval logs `eval {...}` and writes `var.out`. Needs ONNX
  Runtime except for the sandbox.
- **signals**: logs the names of the sandbox's reward signals.

**serve(connected)**: the ML-Agents handshake (communication 1.5.0, package 4.0.0, capabilities incl. hybrid
actions and variable length observations), then the loop: side channel messages (environment parameters as live
values, samplers logged, engine configuration ignored), envparams.txt rewritten on change, parameters pushed to the
worlds when messages came; QUIT exits 0, RESET calls `reset`, STEP calls `step` (an error exits 2); the output is
written and exchanged. A stop signal (CTRL_C) exits 0, a lost connection 2.

**pushEnvParams()**: the resolved parameters (`-set` > live > file) into every world's
[AgentEnvParams](../components/AgentEnvParams.md) (emplaced when missing), which the worlds' systems read at
their episode starts.

## Parameters

Run arguments: `mode` (bench, null, train, record-demo, eval, signals), `threads`, `ticks`; trainer flags
(`--mlagents-port`, `-envParams=`, `-envParamsOut=`, `-set:<name>=<value>`, `-logFile`, ...; `train_args.cpp`).
Scene variables per mode: `decisions`, `out`, `model`, `episodes`, `stochastic`, `replay`, `replay_seconds`, `legs`,
`cot_out`, `checks`, `seconds`, `agents`, `episode_decisions`.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AgentEnvParams](../components/AgentEnvParams.md) | context | write+create | pushEnvParams | the resolved environment parameters (-set > live > -envParams file) in every world, at each connection and on every side channel message |
| [AgentIO](../components/AgentIO.md) | context | readwrite | recordDemo, eval, evalAnimals | observation, sensors, done, recordable, stats read; `reset_requested` set before an eval |
| [PolicyHandle](../components/PolicyHandle.md) | context | write+create | eval, evalAnimals | the eval's policy (var.model) in every world, deterministic unless var.stochastic |
| [WorldClock](../components/WorldClock.md) | context | read | recordDemo, eval, evalAnimals, evalSandbox, writeAnimalChecks | decision period and dt |
| [HumanBot](../components/HumanBot.md) | context | read | eval | the walker's episodes: ticks, falls, legs, energy books, replay segments |
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | animalWorlds, recordAnimalReplay, writeAnimalChecks, evalAnimals | which kind of worlds; segments for replays; finished `episodes` taken and cleared by the eval |
| [AnimalWalkState](../components/AnimalWalkState.md) | context | read | animalDemoProblem | a recording whose walk is not ready is refused with its problem |
| [AnimalDemoChecks](../components/AnimalDemoChecks.md) | context | readwrite | writeAnimalChecks | `foot_slide` (99th percentile) and `speed_error` completed, then written as JSON |
| [Sandbox](../components/Sandbox.md) | context | read | sandboxWorlds, evalSandbox | is this a sandbox scene; species counters, grass, contacts and blows for the summary |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | read | evalSandbox | alive animals' energy and health, replay records |
| [AnimalBot](../components/AnimalBot.md) | component | read | evalSandbox | trunk position, heading, mode and head direction for the replay |

## Side effects

- gRPC client of the ML-Agents trainer (MlAgentsClient on localhost:<port>); reads the environment parameters and engine configuration side channels.
- Files: envparams.txt (-envParamsOut, written on change), .demo (record-demo), eval summaries (var.out), replays (<prefix>.track.*, .replay.*, .sandbox.*, .grass.bin), legs CSV, cot_out, demo checks JSON.
- tf::Executor workers for bench, eval and the worlds' steps.
- Restarts the worlds with the trainer's seed (ScenarioRunner::restart) when the trainer connects in mode=train.
- Logs the mode's progress and summaries; exit codes 0 (QUIT, stop signal), 1 (bad arguments), 2 (connection lost).

## Order and dependencies

- Owns the order of everything: the worlds are stepped by `ScenarioRunner::step` (their own lanes), the
  environment parameters are pushed before the environment is made and after side channel messages.

## Used in scenes
<!-- generated -->

## Tests

- `P3.1` to `P3.5` and the env params tests in [`tests/protocol/null_env.cpp`](../../../tests/protocol/null_env.cpp)
  (mode=null, launch flags, envparams.txt, precedence); `P3.4` in
  [`tests/protocol/ctrl_c.cpp`](../../../tests/protocol/ctrl_c.cpp); `SR.9`, `SR.10` in
  [`tests/protocol/sphere_env.cpp`](../../../tests/protocol/sphere_env.cpp); `MA.*` in
  [`tests/protocol/multi_env.cpp`](../../../tests/protocol/multi_env.cpp).
