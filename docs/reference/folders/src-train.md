# `src/train/`

> [Source](../../../src/train/) · Parent: [`src/`](src.md)

## Summary
The training host of `dynamical_train` (output name TrainingEnv.exe, library `dynamical_train_core`, built with `DYNAMICAL_TRAIN`): headless, no Vulkan, SDL or OpenXR. It parses the Unity-style flags the ML-Agents trainer passes, keeps environment parameters with Unity's precedence and `envparams.txt` format, handles the console stop signal, and serves the trainer through a `TrainerEnvironment`: `NullEnvironment` (fixed observations, no worlds) or `WorldsEnvironment` (one agent per world of a `ScenarioRunner`). The protocol transport is in [`src/mlagents/`](src-mlagents.md).

Entry point `src/train_main.cpp` (`main` of `dynamical_train`): installs the stop handler; `parseTrainArgs(argc, argv)`; opens `-logFile` with `setLogFile` (failure: exit 1); `parseRunArgs(train.rest)`; any train-flag error, unknown `key=value` key or bad value is logged and exits 1. Then a `JoltRuntime`, `registerSimSystems()` (not `registerPolicySystems()` or the view systems), `SceneLoadOptions{ resources = executableDirectory()/"resources", allow_renderer = false }`, scene = `scene=` or, when empty, `null` for `mode=null` and `walker_train` otherwise (no `walker_train.scene.json` exists in `resources/scenes/` at this commit, so an argument-less run fails with "scene not found"). A `ScenarioRunner` with base seed 0 (a `SceneError` exits 1); with `-logFile`, `scene.resolved.json` is written next to the log file; finally `TrainHost(args, train, runner).run()` gives the exit code. It never changes the working directory and never touches `config.6.json`.

## Files
| File | Contents |
|---|---|
| `environment.h` | `TrainerEnvironment` interface: `reset()`, `step(UnityRLInputProto, error)`, `write(UnityOutputProto)` |
| `train_host.h/.cpp` | `TrainHost`: `run`, `bench`, `null`, `agents`, `serve` (handshake, side channels, RESET/STEP/QUIT loop), env-params load and write |
| `worlds_env.h/.cpp` | `WorldsEnvironment`: `check`, `reset`, `step`, `write` over the worlds' `AgentIO` |
| `null_env.h/.cpp` | `NullEnvironment` + `Config { behavior "HeidelBotBehavior", agents 25, episode_decisions 1000, observation_size 138, stack 2, actions 120 }`: zero observations of size 138 x 2, zero rewards, agents truncated after `episode_decisions` STEPs and restarted on the next STEP; sensor name `StackingSensor_size2_VectorSensor_size138`; brain parameters once. No registry access |
| `env_params.h/.cpp` | `EnvParamStore`: `loadFile` (`#` lines skipped, `name value` per line, malformed line or missing file is an error), `setOverride` (`-set:`), `setLive` (side channel), `resolved()` (file < live < override) |
| `env_params_file.h/.cpp` | `EnvParamsFile`, `writeEnvParamsFile` (header `# run=<parent dir name> behavior=<b> written=<steps> key=value...`, then `name value` lines in map order, written to `<file>.tmp<random>` and renamed over), `roundTrip(float)` (shortest `to_chars`) |
| `process_control.h/.cpp` | `installStopHandler` (Windows `SetConsoleCtrlHandler`: CTRL_C, CTRL_BREAK, CTRL_CLOSE set the flag; CTRL_CLOSE sleeps 3 s; no-op elsewhere), `stopRequested`, `stopSignal`, `requestStop`, `clearStop` (atomics) |
| `train_args.h/.cpp` | `TrainArgs`, `parseTrainArgs`: `-nographics`/`-batchmode` ignored, `--mlagents-port <1..65535>`, `-logFile <file>`, `-envParamsOut=`, `-envParams=`, `-gitSha=`, `-set:<name>=<value>`; other `-` flags are errors; everything else goes to `rest` |

## Systems defined here
[TrainHost](../systems/TrainHost.md), [WorldsEnvironment](../systems/WorldsEnvironment.md) (`service`). `NullEnvironment` touches no registry.

## Components defined here
None.

## Subfolders
None.
