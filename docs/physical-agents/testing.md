# Testing

The repo has no tests today. Parity (M1–M3) cannot be claimed without them, so `dynamical_tests`
(doctest) is part of S1. Run it with `ctest --test-dir build -C RelWithDebInfo`.

| Tag | Content |
|---|---|
| `[scene]` | Loader: includes, substitution, type errors, unknown names (each fails with a JSON path), resolved dump round trip; `base` resolves to master's system list in master's order |
| `[world]` | 25 worlds step in parallel; determinism hash equal across two runs; `JoltRuntime` survives creating and destroying worlds repeatedly |
| `[unity]` | `unity_math.h` against values logged in Unity (Euler ZXY, `eulerAngles`, `Angle`, `SignedAngle`, forward) |
| `[frames]` | `simToEngine`: distances kept, right-handed triad out, position and quaternion conversions agree |
| `[body]` | Malformed body files rejected; degrees → radians; mass, centre of mass and inertia in Jolt equal the file; all self-collision disabled; anchors coincide at rest |
| `[rig]` | `rig_anim` headless: rest pose to 1e-5 m and joint angles 0; walk track within limits; bone lengths constant (the M1 gate as a test) |
| `[limits]` | Each `limit_probe` lands on the Jolt limit (twist sign, elliptical cone), within 1° (M2) |
| `[drive]` | Target = rest ⇒ rest; step response at gains (0.5, 0.5) against the Unity recording after gain re-derivation (M2) |
| `[energy]` | Jolt doc checks 1–3 (M2) |
| `[parity]` | Golden files from Unity rollouts (UP §4 M3): load the pose kinematically, run `BodySync`, compare the observation element-wise to 1e-4 (the failing index printed **with its name**), reward terms and termination exactly as logged, action mapping round trip |
| `[protocol]` | Side-channel encoding against byte blobs captured from Python; `MockTrainer` (in-process gRPC server) driving `mode=null` and `mode=train` through init, RESET, STEP × n with done agents, mid-run RESET, QUIT, CTRL_C |
| `[demo]` | `.demo` round trip; `demo_loader` smoke test (`[slow]`, needs Python) |
| `[slow]` | Real-trainer smoke test: `mlagents-learn` with a tiny config against `TrainingEnv.exe` for 2000 steps; asserts exit 0 and the expected stat keys |

**Header boundary.** A CMake test greps the `dynamical_sim` sources for `vulkan`, `SDL`, `imgui`,
`openxr` and `renderer/`.

**Dynamic parity is statistical.** Jolt and PhysX are different solvers, so whole trajectories are
never compared tick by tick. M6 compares behavioural stats at matched updates (UP §4).

## Benchmarks

Recorded in `docs/physical-agents/benchmarks.md` (created at M0): date, commit, machine, build type.

| Gate | Measure |
|---|---|
| M0 | Trainer steps/s against `mode=null` at 6 and 20 envs (Unity reference: about 424 at 20 envs) |
| S2 / M1 | `mode=bench` ticks/s for one world and for 25 worlds, single- and multi-threaded |
| M4 | Decisions/s per process at the training config against the Unity build |
