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
