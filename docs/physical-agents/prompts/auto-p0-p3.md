# Autonomous run: phases P0 → P3

Paste everything below the line into a Claude Code session started at the repo root, or run it
headless:

```
claude -p "$(cat docs/physical-agents/prompts/auto-p0-p3.md)" --permission-mode acceptEdits
```

---

You are implementing phases **P0, P1, P2 and P3** of the HumanBot agent port in this repository
(`C:\Users\Paul\Desktop\Projects\Dynamical`), **fully autonomously**. No human will answer questions
during this run. Work until P3's exit gate passes, or until a stop condition below is reached.

## Sources of truth

Read these before writing code. Re-read the relevant phase section before each phase.
- `docs/physical-agents/phases.md`: **the contract.** Its steps, test IDs, thresholds and exit gates
  for P0–P3 are what you must deliver.
- `docs/physical-agents-plan.md`, and in `docs/physical-agents/`: `architecture.md`, `scenes.md`,
  `simulation.md`, `physics.md`, `mlagents-bridge.md`, `testing.md`, `conventions.md`,
  `jolt-readouts.md`.
- `CLAUDE.md` for the build and the code conventions.
- **The ML-Agents fork** (read-only): `C:\Users\Paul\Desktop\Dynamical-noToes\ml-agents`. It holds the
  protos (`protobuf-definitions/`), the Python trainer (`ml-agents/`, `ml-agents-envs/`) and the C#
  communicator (`com.unity.ml-agents/Runtime/Communicator/`).

If a doc and the code disagree, the code on master is the fact. Fix the doc in the same commit and note
it in the decision log.

## Operating rules

1. **Never ask the user anything.** When a choice is needed:
   - take the option the docs recommend; otherwise the simplest one consistent with them;
   - record it in `docs/physical-agents/reports/decisions.md` (date, phase, decision, why,
     alternatives).
2. **Git:**
   - work on a branch `agents/p0-p3`, created from `master`;
   - commit after every completed numbered step of a phase, with a message `P<n>.<step>: <summary>`
     plus the repository's attribution line;
   - **never push**, never rewrite or delete history, never use `--force`, `reset --hard` or
     `clean -fd` on tracked work.
3. **The uncommitted `src/main.cpp`** (`find_resources()`) is the user's work that the docs build on.
   Commit it first, unchanged, as `main: find resources relative to the executable (user change)`.
4. **Out of bounds:**
   - Never modify `C:\Users\Paul\Desktop\Dynamical`, `C:\Users\Paul\Desktop\Dynamical-noToes` or
     `archive/`.
   - Never change `.claude/` settings or permissions.
   - Never install system-wide software except through vcpkg (manifest mode, this repo) and a Python
     venv inside `build/venv`.
5. **Tests and thresholds are the contract.**
   - **Never weaken a test, raise a threshold, skip a test or re-baseline a golden to get green.** The
     only exception is phases.md's explicit allowance in P2 step 1 (Jolt 5.6 drift, with a written
     explanation in `tests/golden/CHANGELOG.md`).
   - If a threshold proves physically impossible, stop that phase and follow "Stop conditions".
6. **Debug the root cause.** On a failure, read the output and form a hypothesis. Change one thing and
   re-run the narrowest test.
   - After **5 failed attempts on the same failure**, write what you tried into `progress.md`, step
     back, and re-read the relevant source and docs before continuing.
   - After **12**, treat it as a blocker.
7. **Long commands.** vcpkg builds (gRPC and protobuf take 30–60 min the first time), full builds and
   trainer runs go in the background with generous timeouts. Use the build-done notification instead of
   polling. Build with `cmake --build build --config RelWithDebInfo` unless a step needs Debug.
8. **Stay in scope.** Stay inside P0–P3. Do not start P4, and do not refactor beyond what a step needs.
   Keep `base` behaving exactly as master (the P0 goldens and probe are the judge).
9. **Match the surrounding code's style** (CLAUDE.md, conventions.md). Keep comment density low.

## Progress file and resumption

Keep `docs/physical-agents/reports/progress.md` current:
- one line per step: `P<n>.<step> | done/in-progress/blocked | commit sha | note`;
- the last command that failed and why.

**Read it first.** If it exists, resume from the first step that is not done. Never redo committed
steps.

## Per-phase loop

For each phase P0, P1, P2, P3, in order:

1. Re-read the phase section in `phases.md`. List its steps and test IDs in `progress.md`.
2. Implement the steps in order. After each step, build, run the tests that exist so far, and commit.
3. Run the phase's full test set:
   - `ctest --test-dir build -C RelWithDebInfo -L "unit|scenario|protocol" --output-on-failure`;
   - for visual tests, `ctest ... -L gpu`;
   - for `[slow]` tests, the commands phases.md names.
4. **Visual confirmation, agent part.**
   - Generate the phase report: `python tools/phase_report.py P<n>`.
   - **Open every capture, golden, diff heatmap and plot of the phase with the Read tool and look at
     them.**
   - Check each item of the phase's visual checklist against what you see.
   - Write `docs/physical-agents/reports/P<n>.md`. It contains:
     - the commit sha;
     - test results by label;
     - each checklist item ticked `[agent: pass]` or `[agent: FAIL — reason]`, with the image file
       names you inspected;
     - the line `Human sign-off: pending`.

   A visual item you judge failed is a failed gate: fix it like a failing test.
5. **Exit gate.** Every test listed in the phase is green and every checklist item is
   `[agent: pass]`. Then commit the report as `P<n>: exit gate passed`, and continue with the next
   phase without waiting.

## Phase notes (pitfalls already known; details in the docs)

**P0 — Harness**
- Record master's goldens and `base_probe.csv` **before** changing any behaviour. Only the
  test-harness, `RunArgs`, capture, fixed-clock, synchronous-chunk and hidden-window code may exist at
  that point.
- **Determinism sources to neutralise under `capture`:**
  - the wall-clock frame `dt` in `Game::start`;
  - the grass time from `steady_clock` (`renderer.cpp:58`, `:100`);
  - asynchronous chunk generation and collider prefetch on Taskflow. Wait for an empty chunk queue
    before the first shot.
- **Capture:** the swapchain needs `eTransferSrc`. Read back after the frame fence, write PNGs with
  `stb_image_write` (stb is in `vcpkg.json`). Size 960 × 540, `vr_mode=0`, hidden window.
- **CI:** create `.github/workflows/ci.yml` (Windows) and the `ci` preset. You cannot run CI here, so
  make sure the same `ctest -L "unit|scenario|protocol"` passes locally with that preset, and note
  "CI file not executed remotely" in the report.

**P1 — Library split and scenes**
- Library-boundary offenders, per `architecture.md`: `game.*`, `scene.cpp` / `scene_spawn.h`,
  `time.cpp`, `components/input.h`, and the `vulkan.h` in `system.h`.
- `spawnBox` stays view-side, because `VRControl` uses it.
- `ModelRenderable` and its registry entry are `dynamical`-only; the library gets `ModelRef`.
- `PlayerControl` creates the `Camera` ctx that `ClassicRender` needs.
- `BoxRing` must run its `init` after `ChunkGenerator`'s `preinit`.
- **Registry names are not `System::name()`.**
- `dynamical_train` must not construct `Settings` or change the working directory. T1.7 checks both.
- **V1.1 and S1.1 compare against the P0 goldens and probe with no re-baseline.** If they differ, the
  port changed behaviour. Find out why.

**P2 — Worlds and Jolt 5.6**
- Jolt 5.6.0 comes **only through an overlay port** (`ports/joltphysics`), GPU compute options off.
  **Do not bump the vcpkg baseline.** Fetch the port files from upstream vcpkg's
  `ports/joltphysics` at the 5.6.0 version.
- Move the Jolt registration into `JoltRuntime`. `PhysicsWorld`'s job-system member becomes
  `unique_ptr<JPH::JobSystem>`.
- `WorldView` primitives go in `resources/models/primitives/`. Capsules are generated per (radius,
  half-height), not scaled.
- Update the `base` system-list test after the `PhysicsSys` split (T2.4).
- Create `docs/physical-agents/benchmarks.md`.

**P3 — Bridge skeleton, null env**
- **Protos.** Vendor them from the fork's `protobuf-definitions/proto/mlagents_envs/communicator_objects/`
  and record the fork commit (`git -C …/Dynamical-noToes rev-parse HEAD`) in
  `src/mlagents/proto/VERSION`. Generate code with vcpkg's protoc and `grpc_cpp_plugin`
  (`protobuf_generate`).
- **Check every *verify* item in `mlagents-bridge.md` against the fork**, and update the doc with
  file:line evidence:
  - the double exchange at initialisation;
  - the action order;
  - the side-channel UUID byte order;
  - `package_version`;
  - the exact stacking sensor name.

  Already verified: the worker arguments, stable agent ids, and the seed already including
  `worker_id`.
- **Python** for the side-channel blobs, `train_smoke.py` and B3.1:
  - create `build/venv`;
  - `pip install -e` the fork's `ml-agents-envs` and `ml-agents` from the read-only tree. If an
    editable install would write into that tree, use a non-editable install.
- **CTRL_C:** handle C, BREAK and CLOSE; use a gRPC deadline of 1 s; exit 0 once the flag is set.
  `dy::log` gets a `-logFile` sink flushed per line.
- **B3.1:** 3 runs each at 6 and 20 envs, appended to `benchmarks.md`. Write the decision note the
  P3 exit gate asks for.
- **The Unity-side task** "freeze the reference runs" is not yours. List it as an open item in
  `reports/P3.md`.

## Stop conditions

Stop working and write the final summary when any of these holds:
- **P3's exit gate has passed.** This is the success case.
- **A blocker:**
  - 12 failed attempts on one failure;
  - a threshold that is physically impossible, with evidence;
  - a missing dependency that cannot be installed under rule 4;
  - a required decision that would contradict a doc's explicit decision.

  Write `docs/physical-agents/reports/BLOCKED.md`: phase, step, evidence, what you tried, and the
  options with a recommendation. Commit it, then stop.
- **The workspace would need destructive action** to proceed.

## Final summary (your last message)

- Phases completed, with the commit sha of each exit gate.
- Test totals by label, plus every test that is not green (there must be none for completed phases).
- A link to each `reports/P<n>.md`, and the list of visual items awaiting human sign-off.
- The decisions logged, and the docs changed.
- Benchmarks recorded (B2.1, B3.1) and the M0 throughput conclusion.
- Open items for the human: visual sign-offs, the Unity-side reference runs, any `BLOCKED.md`.
- The branch name and the reminder that nothing was pushed.
