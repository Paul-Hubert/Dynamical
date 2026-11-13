# src/train

The training host, `dynamical_train` (`TrainingEnv.exe` to the trainer, main in
[`src/train_main.cpp`](../../../src/train_main.cpp)): no window, no frame loop, the scene's worlds step as fast as the
CPU allows, and the engine plays Unity's part of the ML-Agents protocol 1.5.0 against the Python trainer
([ML-Agents bridge](../../physical-agents/mlagents-bridge.md), [simulation](../../physical-agents/simulation.md)).
Built as `dynamical_train_core` with gRPC and protobuf; the modes are bench, null, train, record-demo, eval and
signals.

## Files

| File | What |
|---|---|
| [`train_host.h`](../../../src/train/train_host.h), [`train_host.cpp`](../../../src/train/train_host.cpp) | `TrainHost`: `run` (the modes), `serve` (handshake, RESET / STEP / QUIT loop, side channels), bench, null, train, record-demo (`.demo` files), the walker's eval, environment parameters (`loadEnvParams`, `writeEnvParams`, `pushEnvParams`) |
| [`animal_host.cpp`](../../../src/train/animal_host.cpp) | `TrainHost`'s animal side: `animalWorlds`, `animalDemoProblem`, `recordAnimalReplay`, `writeAnimalChecks` (demo checks JSON, replays), `evalAnimals` |
| [`sandbox_host.cpp`](../../../src/train/sandbox_host.cpp) | `TrainHost`'s sandbox side: `sandboxWorlds`, `evalSandbox` (summary per species, series, replay and grass files), `signals` |
| [`environment.h`](../../../src/train/environment.h) | `TrainerEnvironment`: the interface `serve` drives (`reset`, `step`, `write`) |
| [`worlds_env.h`](../../../src/train/worlds_env.h), [`worlds_env.cpp`](../../../src/train/worlds_env.cpp) | `WorldsEnvironment`: the agents of every world (context and components), gathered by behaviour, actions in, windows stepped in parallel until one asks or ends, AgentInfos and stats out |
| [`null_env.h`](../../../src/train/null_env.h), [`null_env.cpp`](../../../src/train/null_env.cpp) | `NullEnvironment` (mode=null): N agents of one behaviour, zero observations, truncated episodes; the trainer's own ceiling |
| [`env_params.h`](../../../src/train/env_params.h), [`env_params.cpp`](../../../src/train/env_params.cpp) | `EnvParamStore`: environment parameters by source, `-set:` > live > `-envParams=` file |
| [`env_params_file.h`](../../../src/train/env_params_file.h), [`env_params_file.cpp`](../../../src/train/env_params_file.cpp) | `EnvParamsFile`, `writeEnvParamsFile` (Unity's envparams.txt, header and ordinal lines, written to a temporary then renamed), `roundTrip` (shortest float text) |
| [`train_args.h`](../../../src/train/train_args.h), [`train_args.cpp`](../../../src/train/train_args.cpp) | `TrainArgs`, `parseTrainArgs`: the Unity style flags the trainer passes (`--mlagents-port`, `-logFile`, `-envParamsOut=`, `-envParams=`, `-set:`, `-gitSha=`), the rest as run arguments |
| [`process_control.h`](../../../src/train/process_control.h), [`process_control.cpp`](../../../src/train/process_control.cpp) | the stop handler (CTRL_C, CTRL_BREAK, CTRL_CLOSE set a flag; `stopRequested`, `stopSignal`, `requestStop`, `clearStop`) |

## Systems

None registered. Services that touch the worlds' registries:

- [WorldsEnvironment](../systems/service-WorldsEnvironment.md): mode=train's environment
- [TrainHost](../systems/service-TrainHost.md): the modes, environment parameters, evals

## Components and context variables

None defined. Touched in the worlds: [AgentIO](../components/AgentIO.md) (context and components),
[AgentsReset](../components/AgentsReset.md), [AgentEnvParams](../components/AgentEnvParams.md),
[PolicyHandle](../components/PolicyHandle.md), [WorldClock](../components/WorldClock.md),
[HumanBot](../components/HumanBot.md), [AnimalBot](../components/AnimalBot.md),
[AnimalWalkState](../components/AnimalWalkState.md), [AnimalDemoChecks](../components/AnimalDemoChecks.md),
[Sandbox](../components/Sandbox.md), [SandboxAnimal](../components/SandboxAnimal.md).

## Worth knowing

- The trainer is the gRPC server; the engine calls `Exchange` (via `src/mlagents/`), and only answers when an agent
  asks for a decision or ends, as Unity does. A done agent gets no action in the next STEP.
- In mode=train the worlds are restarted with the trainer's seed when it connects (`ScenarioRunner::restart`);
  agents of a world context have the world's index as id, components get ids after them, kept over episodes.
- Stats are averaged per key over a window and sent in the stats side channel with their aggregation (average,
  most recent, sum, histogram).
- Exit codes: 0 on QUIT, header 400 or a stop signal; 1 on bad arguments; 2 when the connection is lost or a STEP is
  malformed.

## Dependencies

- `src/mlagents/` (client, side channels, the generated protos), `src/scene/` (`ScenarioRunner`, `RunArgs`,
  `WorldClock`), `src/logic/agents/`, `src/logic/animals/`, `src/logic/sandbox/`, `src/onnx/` (with ONNX Runtime),
  `src/util/` (log, `profile::namedWorkers`), Taskflow, protobuf.
- Used by `src/train_main.cpp` and `tests/protocol/`.

## Design docs

- [docs/physical-agents/mlagents-bridge.md](../../physical-agents/mlagents-bridge.md)
- [docs/physical-agents/simulation.md](../../physical-agents/simulation.md)
- [docs/action-agents.md](../../action-agents.md) (several agents in a world, the sandbox's eval and rounds)
