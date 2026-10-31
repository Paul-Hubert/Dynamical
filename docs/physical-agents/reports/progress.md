# Progress: autonomous run P0 → P3

Branch `agents/p0-p3`. One line per step: `P<n>.<step> | status | commit | note`.

## P0 — Test and capture harness

Tests: T0.1 (unit), V0.1, V0.2 (gpu), S0.1 (probe, gpu until P1 splits the library).

| Step | Status | Commit | Note |
|---|---|---|---|
| user | done | 3359744 | `main: find resources relative to the executable (user change)` |
| P0.1 | in-progress | | doctest, `dynamical_tests`, labels |
| P0.2 | | | `RunArgs` |
| P0.3 | | | fixed frame clock, grass time |
| P0.4 | | | synchronous chunks, no prefetch under capture |
| P0.5 | | | hidden window |
| P0.6 | | | capture |
| P0.7 | | | imgdiff, rebaseline, phase_report |
| P0.8 | | | probe |
| P0.9 | | | CI |
| P0.10 | | | master baseline |

## Last failure

(none)
