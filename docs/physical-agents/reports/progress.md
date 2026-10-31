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
| P0.10 | done | (this commit) | master baseline |

## Last failure

(none)
