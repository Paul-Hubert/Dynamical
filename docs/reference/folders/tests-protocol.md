# `tests/protocol/`

> [Source](../../../tests/protocol/) · Parent: [`tests/`](tests.md)

## Summary
Suite `protocol`: the ML-Agents bridge of `dynamical_train`, compiled only with `DYNAMICAL_TRAIN`. Tests run `TrainHost` in-process (or TrainingEnv.exe as a child process) against `MockTrainer` (`tests/support/mock_trainer.*`), an in-process gRPC server that answers like the fork's Python `RpcCommunicator`.

## Files
| File | Contents |
|---|---|
| `null_env.cpp` | P3.1 init, empty message, RESET, 100 STEP, QUIT on `mode=null`; header 400 ends the run with exit 0; P3.3 launch flags, `-set:x=1`, missing `-envParams=` exits 1; P3.5 `envparams.txt` header, order, round-trip floats, sha; env-param precedence |
| `sphere_env.cpp` | SR.9 `mode=train` on `sphere_roll`: RESET, 60 STEP, QUIT; SR.10 refusal of a scene without agent worlds |
| `side_channels.cpp` | P3.2 decode the blobs in `tests/data/sidechannel/` and re-encode byte for byte; truncated payloads refused |
| `ctrl_c.cpp` | P3.4 (Windows only): CTRL_C to TrainingEnv.exe in its own process group exits 0 within 2 s with a log line |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
