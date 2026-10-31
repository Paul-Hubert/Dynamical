# Progress: autonomous run P0 → P3

Branch `agents/p0-p3`. One line per step: `P<n>.<step> | status | commit | note`.

## P0 — Test and capture harness

Tests: T0.1 (unit), V0.1, V0.2 (gpu), S0.1 (probe, gpu until P1 splits the library).

| Step | Status | Commit | Note |
|---|---|---|---|
| user | done | 3359744 | `main: find resources relative to the executable (user change)` |
| P0.1 | done | e44e37e | doctest, `dynamical_tests`, labels |
| P0.2 | done | bb23409 | `RunArgs` |
| P0.3 | done | 22b3d60 | fixed frame clock, grass time |
| P0.4 | done | 32e4c4c | synchronous chunks, no prefetch under capture |
| P0.5 | done | 8817253 | hidden window |
| P0.6 | done | 4fe3c2b | capture |
| P0.7 | done | 2552513 | imgdiff, rebaseline, phase_report |
| P0.8 | done | 5d57f76 | probe |
| P0.9 | done | 9605a57 | CI |
| P0.10 | done | dc2b31a | master baseline |
| P0 gate | done | (exit gate commit) | unit 10/10, gpu 4/4; report `reports/P0.md` |

## P1 — Library split and scene system

Tests: T1.1, T1.3, T1.4, T1.5, T1.6 (unit), T1.2 (unit, import table), T1.7 (scenario), S1.1, V1.1 (gpu).

| Step | Status | Commit | Note |
|---|---|---|---|
| P1.1 | done | 80b9934 | `vulkan.h` out of `system.h` |
| P1.2 | done | 9a001d1 | `TimeSys` to the executable |
| P1.3 | done | 853ed19 | CMake targets and library-boundary moves |
| P1.4 | done | 6748c55 | T1.1 header boundary |
| P1.5 | done | 1aa4eda | scene core, registries, prefabs |
| P1.6 | done | 853d56e | `SystemSet` fixed lane |
| P1.7 | done | a082538 | renderer pass list from the scene |
| P1.8 | done | 48c20c1 | `base.scene.json` |
| P1.9 | done | 88664b5 | `train_main.cpp` |
| P1 check | done | b37f099 | scripted input in capture scripts (interactive checklist item) |
| P1 gate | done | (exit gate commit) | unit 22, scenario 1, gpu 7; report `reports/P1.md` |

## P2 — Worlds, Jolt 5.6, flat ground

Tests: T2.1, T2.2, T2.4, T2.5 (unit), S2.1–S2.4 (scenario), B2.1 (bench), V2.1, V2.2 (gpu).

| Step | Status | Commit | Note |
|---|---|---|---|
| P2.1 | done | 9094118 | Jolt 5.6.0 overlay; drift explained, goldens re-recorded |
| P2.2 | done | cb04422 | `JoltRuntime` |
| P2.3 | done | f432505 | `PhysicsConfig`, `PhysicsWorld` in the ctx |
| P2.4 | done | 8f29930 | `PhysicsSys` split |
| P2.5 | done | 70a43b2 | layers |
| P2.6 | done | 3aa28db | worlds, `TrainHost` bench, `WorldStepper` |
| P2.7 | done | b100b06 | `Ground`, combine callbacks, capsules |
| P2.8 | done | cf3b065 | `WorldView` |
| P2.9 | done | 46d0b48 | test scenes |
| P2.10 | done | 0eaba61 | `benchmarks.md` |
| P2 gate | done | (exit gate commit) | unit 28, scenario 5, gpu 10; report `reports/P2.md` |

## P3 — Bridge skeleton and null environment

Tests: P3.1–P3.5 (protocol), L3.1 (slow, `tools/train_smoke.py`), B3.1 (`tools/bench_null.py`).

| Step | Status | Commit | Note |
|---|---|---|---|
| P3.1 | done | ee7d0d9 | gRPC, protobuf, vendored protos |
| P3.2 | done | e3c9cd1 | `MlAgentsClient` |
| P3.3 | done | b318d47 | side channels |
| P3.4 | done | cf60c24 | `train_args` |
| P3.5 | done | efcd032 | log file sink, CTRL_C |
| P3.6 | done | 1d901b9 | `mode=null` |
| P3.7 | done | e063bd0 | `MockTrainer` |
| P3.8 | done | (this commit) | `train_smoke.py` |

P3.1–P3.7 were written together while gRPC built; each commit holds its step's files, and only the P3.7 commit
builds on its own (the CMake of P3.1 already names the later sources).

## Last failure

- L3.1 first runs: `WANDB_MODE=disabled` made the fork's `wandb.init` raise, dropping the TensorBoard writer (no
  events); then `WANDB_MODE=offline` was overridden by the fork and one run (`null_smoke`) reached wandb.ai.
  Fixed by `tools/wandb_offline/sitecustomize.py` (decisions.md, incident).
