# Testing

The repo has no tests today. Parity with Unity (M2, M3, M6) is impossible to claim without them, so the
test target is part of M1.

## Target

`dynamical_tests` (doctest via vcpkg), linking `dynamical_sim` and `dynamical_mlagents`. Run with
`ctest --test-dir build -C Release`. Test data in `tests/data/` (small) and `resources/agents/` (shared
with the engine). Tags: `[unit]`, `[physics]`, `[parity]`, `[protocol]`, `[slow]`.

## Unit tests

| Area | Tests |
|---|---|
| Frames | `simToMap` keeps distances, maps a right-handed triad to a right-handed triad, quaternion and position conversions agree |
| Body loading | malformed files rejected; degrees → radians; parents-before-children |
| Ragdoll build | masses, COM and inertia in Jolt equal the body file; parent–child collisions disabled; rest pose with zero gravity stays put to 1e-6 over 100 ticks |
| Joint limits | every `limit_probe` from the body file lands exactly on the Jolt limit (twist low/high, swing Y/Z), including the twist sign convention |
| Drives | target = identity ⇒ rest pose; step response of a single driven joint (rise time, overshoot) vs values recorded in Unity |
| Side channels | UUID byte order (`bytes_le`), string encoding, env-param FLOAT/SAMPLER parsing, stats encoding; golden byte blobs captured from the Python side |
| Demo writer | round trip; metadata ≤ 33 bytes |
| Determinism | two 1000-tick runs with the same seed and random actions give identical state hashes |

## Physics checks (M2)

The three energy checks in [physics.md](physics.md#energy), plus:

- Ragdoll stands in rest pose with drives holding the rest targets for 10 s without drifting more than
  the Unity plan's tolerance.
- Limit envelope: random actions for 60 s never exceed any limit by more than 1°.
- Contact: a dropped box rests on the ground with penetration < `mPenetrationSlop`.

## Parity tests against Unity (M3)

Golden files exported from Unity (a small exporter extension, run once per body/observation version):

`tests/data/golden/<body>/<case>.json`: `{ "pose": [...segment states...], "command": {...},
"env_params": {...}, "observations": [[...]], "rewards": {"term": value, ...}, "terminated": bool }`.

The test loads the pose into a ragdoll (kinematic set, as the demo recorder does), runs `BodySyncSys`,
then calls `ObservationLayout::write`, every `RewardTerm::evaluate` and every `TerminationRule::check`,
and compares:

- observations element-wise, `|a − b| ≤ 1e-4` (float32 arithmetic differs between C# and C++), with the
  failing index printed **with its name** from the layout;
- rewards per term within `1e-4` relative;
- terminations exactly.

Cases: rest pose, mid-stride frames from walk tracks, a fallen pose, extreme joint angles at limits,
slope-rotated ground, every command direction quadrant. Because the sim keeps Unity's numbers
([conventions.md](conventions.md#why-the-sim-keeps-unitys-numbers-unchanged)), no frame conversion
happens anywhere in these tests.

Dynamic parity (whole trajectories) is **not** expected to match tick by tick: Jolt and PhysX are
different solvers. It is judged statistically at M6 (behavioural stats at matched updates).

## Protocol tests

- `MockTrainer`: an in-process gRPC server (C++) implementing the Python side's sequence (init, empty,
  RESET, STEP × n, QUIT) against `dynamical_train --mode null-env`. Checks action routing with done
  agents in the batch.
- Real-trainer smoke test (`[slow]`, opt-in): `mlagents-learn` with a tiny PPO config and
  `--env=dynamical_train --env-args --mode null-env` for 2000 steps; asserts exit code 0 and that the
  run directory contains TensorBoard events with the custom stats.

## Performance gates

| Gate | Measure | Where |
|---|---|---|
| M0 | trainer steps/s at 6 and 20 env processes, null env | real trainer |
| M1 | physics ticks/s for one ragdoll in one arena, single thread | `--mode bench` |
| M4 | decisions/s per env process at the training config (N arenas, decision period) vs the Unity build | `--mode bench` and real trainer |
| M9 | frame time with `max_physical_walkers` agents in the game | Optick |

Record results in `docs/physical-agents/benchmarks.md` (create it at M0) with date, commit, machine and
build type, so regressions are visible.

## Header-boundary check

A CI step (or a test that greps sources) fails if any file under `src/logic` (except the game-only list
in [architecture.md](architecture.md#game-only-files-stay-in-the-dynamical-exe)), `src/ai`,
`src/physics` or `src/agent` includes `vulkan`, `SDL`, `imgui` or `renderer/`. Building
`dynamical_train` without Vulkan installed is the real check; do it on NixOS in a shell without the
Vulkan packages.
