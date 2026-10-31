# Testing

The repo has no tests today. The rig, the features and the energy books cannot be trusted without
them, so `dynamical_tests` (doctest) is part of S1. Run it with
`ctest --test-dir build -C RelWithDebInfo`.

| Tag | Content |
|---|---|
| `[scene]` | Loader: includes, substitution, type errors, unknown names (each fails with a JSON path), resolved dump round trip; `base` resolves to master's system list in master's order |
| `[world]` | 25 worlds step in parallel; determinism hash equal across two runs; `JoltRuntime` survives creating and destroying worlds repeatedly |
| `[import]` | Unity importer: right-handed triad after conversion; anchors coincide at rest; masses = percentage × total (sum 99.98 %); foot scale baked; triggers dropped; knee rest moved onto its stop; swing clamp logged |
| `[body]` | Mass, centre of mass and inertia in Jolt equal the body file; every self-collision pair disabled; max angular velocity and damping as the body version says |
| `[rig]` | `rig_anim` headless: rest pose to 1e-5 m; walk track within limits except where the clip exceeds them (reported); bone lengths constant; no velocity spike at seams (the M1 gate as a test) |
| `[limits]` | Each `limit_probe` lands on the Jolt limit (twist sign after reflection, cone shape), within 1°; knee low stop 2°, ankle low −38°, knee Y 15° |
| `[drive]` | Target = rest ⇒ rest; ω mapping linear (`e · maxTargetAngularVelocity`); action 0 ⇒ mid-twist; step response at gains (0.5, 0.5) against the Unity recording after the gain scale is derived (M2) |
| `[energy]` | Jolt doc checks 1–3; closure residual < 1–2 % on random-action rollouts; contact work 0 in flight |
| `[task]` | Header frame is orthogonal and yaw-only; kill threshold depends only on `dyingHeightFactor` and the rest pose; no anchor-velocity spike after random yaw or random init; reset order; `env_target_speed_overrides_draw`; first-decision reward skip; terminal-window reward counted |
| `[oracle]` | Unity golden files (per-tick poses, observations, rewards from Unity rollouts). Converted by the importer, then checked: the `heidelbot_notoes_1` oracle layout reproduces Unity's observations to 1e-4 (failing index printed **with its name**); the reward terms and kill events reproduce the logs. This validates the rig, frames and features; the trained `_2` layout is checked against `_1` on the shared quantities (head error, joint axes up to the frame change, anchor velocities) |
| `[protocol]` | Side-channel encoding against byte blobs captured from Python; `MockTrainer` (in-process gRPC server) driving `mode=null` and `mode=train` through init, RESET, STEP × n with done agents, mid-run RESET, QUIT, CTRL_C; `envparams.txt` format |
| `[demo]` | `.demo` round trip; header x,z zero in recordings; zero actions; `demo_loader` smoke test (`[slow]`, needs Python) |
| `[slow]` | Real-trainer smoke test: `mlagents-learn` with a tiny config against `TrainingEnv.exe` for 2000 steps; asserts exit 0 and the expected stat keys |

**Header boundary.** A CMake test greps the `dynamical_sim` sources for `vulkan`, `SDL`, `imgui`,
`openxr` and `renderer/`.

**Behaviour is judged statistically.** Jolt and PhysX are different solvers, and the trained layout is
Jolt-native, so trajectories are never compared tick by tick. M6 compares behavioural stats at matched
gradient updates: dead rate, episode length, root speed against target, head height, and the DeVita
stats for the energetics line.

## Benchmarks

Recorded in `docs/physical-agents/benchmarks.md` (created at M0): date, commit, machine, build type.

| Gate | Measure |
|---|---|
| M0 | Trainer steps/s against `mode=null` at 6 and 20 envs (Unity reference: about 424 at 20 envs) |
| S2 / M1 | `mode=bench` ticks/s for one world and for 25 worlds, single- and multi-threaded |
| M4 | Decisions/s per process at the training config against the Unity build |
