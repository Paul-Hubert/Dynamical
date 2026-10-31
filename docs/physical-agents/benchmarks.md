# Benchmarks

Appended by phase (phases.md, numeric artefacts): date, commit, machine, build type, command, result.
Machine for every entry so far: AMD Ryzen 9 9950X3D (16 cores, 32 threads), 61.6 GB RAM, Windows 11 Pro
10.0.26200, RTX 5080 (unused by these), MSVC 14.44, RelWithDebInfo.

## B2.1 — `mode=bench` on `test_boxes` (P2)

2026-10-31, commit `46d0b48`. `TrainingEnv.exe scene=test_boxes mode=bench var.num_worlds=<w> threads=<t> ticks=1200`.
Each world: 50 cubes dropped on a 24 m ground, Jolt 5.6.0 single-threaded, 60 Hz, sleeping off. The 1200
ticks include the drop (about the first 60 ticks) and 19 s of resting contact. Three runs each.

| Worlds | Workers | ticks/s (mean ± sd) | world-ticks/s (mean) |
|---|---|---|---|
| 1 | 1 | 28 443 ± 134 | 28 443 |
| 1 | 32 | 28 568 ± 155 | 28 568 |
| 25 | 1 | 1 164.5 ± 4.9 | 29 112 |
| 25 | 32 | 19 049 ± 239 | 476 230 |

- One world costs about 35 µs per tick (50 resting bodies), and the cost is linear in the number of worlds
  on one worker (25 worlds: 29 112 world-ticks/s against 28 443 for one).
- 25 worlds on 32 workers scale 16.4× over one worker (the 16 physical cores); one world gains nothing
  from more workers since each world is single-threaded by design (physics.md, `threads=1`).

## B3.1 — trainer steps/s against `mode=null` (P3, the M0 gate)

2026-10-31, commit `ba64243`. The fork's `mlagents-learn` (`build/venv`, torch 2.14.1 CPU) against
`TrainingEnv.exe mode=null` (25 agents per process, zero 276-float observation, 120 actions, episodes of 1000
decisions), wandb offline. `tools/bench_null.py --envs 6 20 --runs 3 --steps 60000` with `tests/data/null_ppo.yaml`
(PPO, 64 × 1 network, buffer 5120). Steps/s: slope of the trainer's own `Step: N. Time Elapsed: T s` lines
from the first to the last summary of each run; updates included.

| Envs | steps/s (mean ± sd) | runs |
|---|---|---|
| 6 | 7 343.9 ± 159.7 | 7 174.5, 7 491.6, 7 365.6 |
| 20 | 8 967.9 ± 172.7 | 8 783.5, 9 125.8, 8 994.4 |

Supplementary, same day: the walker's PPO settings of `mlagents-config/default-training.yaml` (512 × 3 network,
normalisation, batch 5000, buffer 50000, 2 epochs) without GAIL, which needs demos
(`tests/data/null_walker_ppo.yaml`, `--envs 20 --runs 1 --steps 400000 --tag _walker`): **4 342.4 steps/s**.

Plot: `build/artifacts/P3/plots/throughput.png` (`tools/plot_throughput.py`), with the Unity reference at 424.

### M0 decision note

The Unity build reached about 424 steps/s at 20 envs. Against the null environment the same trainer takes
8 968 steps/s with a small network and 4 342 steps/s with the walker's network, batch and epochs: 10 to 21 times
the Unity figure. **The trainer was not the ceiling; the Unity environment was.** Engine speed is therefore what
bounds training throughput, and plan D2 stands: keep gRPC, protocol 1.5.0 and the unchanged trainer (nothing to
gain from replacing it while the environment is 10× slower than it). Open caveats: GAIL's discriminator update
is not in these numbers (it needs the P9 demos; B10.1 measures the full config), and the trainer side was CPU torch
on this machine.
