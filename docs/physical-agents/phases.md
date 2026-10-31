# Implementation phases

The executable plan: **16 phases**, each with ordered steps, automated tests with numeric thresholds,
a visual confirmation, and an exit gate. A phase is done when its gate passes in CI (or in the local
GPU job for `[gpu]` tests) **and** its visual confirmation has been reviewed and signed off in the
phase report.

The design lives in the other docs; this file says in which order to build it and how to prove each
step. Phase numbers map onto the plan's milestones:

| Phase | Milestone | Title |
|---|---|---|
| P0 | — | Test and capture harness |
| P1 | S1 | Library split and scene system |
| P2 | S2 | Worlds, Jolt 5.6, flat ground |
| P3 | M0 | Bridge skeleton and null environment |
| P4 | M1 | Exporter, importer, ragdoll at rest |
| P5 | M1 | `rig_anim` track playback |
| P6 | M2 | Joint limits and drives |
| P7 | M2 | Contacts, friction, exact energy (Jolt patches) |
| P8 | M3 | Agent runtime and Unity oracle |
| P9 | M5 | Demo recorder (before the full bridge, so M4 can gate on an unmodified GAIL config) |
| P10 | M4 | Full bridge and real training runs |
| P11 | M5b | ONNX inference and `walker_view` |
| P12 | M6 | First GAIL walker |
| P13 | M7 | DeVita energetics and slope |
| P14 | M8 | Eval mode and replays |
| P15 | M9 | Extended lines |

---

## Test and review system (built in P0, used by every phase)

### Test levels

| Level | Runner | Label | Where it runs |
|---|---|---|---|
| Unit | `dynamical_tests` (doctest) | `unit` | CI (Windows, no GPU) and locally |
| Scenario: a scene run headless for N ticks, asserting on state | `dynamical_tests` via `ScenarioRunner`, or `dynamical_train scene=… mode=bench ticks=N assert=…` | `scenario` | CI and locally |
| Protocol: in-process `MockTrainer` gRPC server | `dynamical_tests` | `protocol` | CI and locally |
| Visual: a scene rendered with a scripted camera at a fixed frame time, captured to PNG and diffed against a golden | `dynamical capture=<script>` + `imgdiff` | `gpu` | Local GPU job (dev machine or a self-hosted runner). In CI, optional on SwiftShader |
| Trainer: real `mlagents-learn` against `TrainingEnv.exe` | `tools/train_smoke.py` | `slow` | Local, nightly |

Commands:

```
ctest --test-dir build -C RelWithDebInfo -L "unit|scenario|protocol"     # CI on every push
ctest --test-dir build -C RelWithDebInfo -L gpu                          # local / self-hosted GPU
python tools/phase_report.py P<n>                                        # builds the phase report
```

### Capture facility (P0)

- **`dynamical capture=<script.json>`** runs any scene with:
  - `fixed_frame_dt=1/60`: the frame clock ignores the wall clock;
  - `window_hidden=true` (an SDL hidden window, so the swapchain still exists);
  - `vr_mode=0`.

  It moves the camera along the script, writes PNGs at the listed frames and exits.
- **Implementation.**
  - The swapchain is created with `eTransferSrc` usage.
  - After the frame's graphics submit, the acquired image is copied to a host-visible buffer, read after
    the frame fence and written with `stb_image_write`.
  - The capture size is fixed at 960 × 540 (the window size is set to match), so goldens are small.
- **Script format.**

  ```jsonc
  { "scene": "rig_anim", "vars": { "track": "agents/tracks/heidelbot_notoes__BasicMotions@Walk.track.json" },
    "shots": [ { "frame": 120, "camera": { "pos": [2.5, 1.2, 2.5], "look_at": [0, 0.9, 0] }, "name": "walk_side" } ] }
  ```

- **Determinism.** With a fixed frame dt, fixed world seeds and grass wind driven by the frame clock,
  two captures of the same script on the same machine must match (P0 test V0.1).
- **`imgdiff`** (`tools/imgdiff`, C++ with stb) compares a capture to `tests/golden/images/<name>.png`.
  It fails if the mean absolute error exceeds **1.5 / 255**, or if more than **0.5 %** of pixels differ by
  more than **24 / 255**. On failure it writes a diff heatmap next to the capture.
- **Re-baselining.** Goldens change only on purpose: `tools/rebaseline.py <name> --reason "<text>"`
  copies the new capture and appends the reason to `tests/golden/CHANGELOG.md`. Golden changes are
  reviewed in the same commit.

### Phase report (visual sign-off)

`tools/phase_report.py P<n>` builds `build/reports/P<n>/index.html` containing:
- the test results by label;
- every capture of the phase next to its golden and diff heatmap;
- the plots the phase generates (CSV → matplotlib PNG);
- side-by-side Unity images where the phase has them;
- the phase's **visual checklist** from this doc, with a checkbox per item.

Signing off means committing `docs/physical-agents/reports/P<n>.md`: the checklist with the boxes
ticked, the commit sha, and the names of the reviewed images. That file is the record that a person
looked.

### Numeric artefacts

- Every scenario and benchmark test writes CSV under `build/artifacts/P<n>/`.
- Plots are produced from these CSVs, never from screenshots of ImGui, so they can be regenerated and
  diffed.
- Benchmarks are appended to `docs/physical-agents/benchmarks.md` (date, commit, machine, build type).

---

## P0 — Test and capture harness

**Goal.** Before changing anything, pin master's current behaviour and build the tools every later
phase uses.

**Steps.**
1. Add `doctest` to `vcpkg.json`. Create `tests/` with a `dynamical_tests` target that, for now, links
   the existing sources, plus `enable_testing()` and labels.
2. **`RunArgs`** (architecture.md "Run arguments") carries the new keys: `capture`, `fixed_frame_dt`,
   `window_hidden`, `exit_after_capture`, `probe*`. `Settings` is untouched.
3. **Fixed frame clock.** `fixed_frame_dt` replaces the wall-clock `dt` in `Game::start`. The renderer's
   grass time also reads the frame clock (`renderer.cpp:58`, `:100` use `steady_clock` today).
4. **Deterministic terrain for captures.** Under `capture`, `ChunkGeneratorSys` runs synchronously and
   the terrain-collider prefetch is disabled. The capture waits until the chunk queue is empty before
   the first shot frame. Otherwise which chunks are meshed by frame N depends on Taskflow timing.
5. **Hidden window.** `window_hidden` makes `Windu` create the window with `SDL_WINDOW_HIDDEN`
   (`windu.cpp:29` uses `SDL_WINDOW_SHOWN`). `window_width` / `window_height` set the 960 × 540 capture
   size.
6. **Capture:** swapchain `eTransferSrc`, readback, PNG, script runner, `exit_after_capture`.
7. `tools/imgdiff`, `tools/rebaseline.py`, `tools/phase_report.py`.
8. **Behaviour probe.** `probe=<file.csv> probe_entities=RigidBody probe_every=60` dumps
   `Transform`s each N frames, so physics behaviour can be pinned numerically.
9. **CI.** A GitHub Actions workflow on Windows: the `ci` preset (`DYNAMICAL_BUILD_VIEWER=OFF`, so no
   Vulkan SDK), vcpkg binary cache, build, `ctest -L "unit|scenario|protocol"`. Until P1 splits the
   library, the CI job builds `dynamical_tests` only from the files that do not need Vulkan (the doctest
   smoke case and `imgdiff`).
10. **Record master's baseline:**
   - `base_spawn.png`: spawn view at frame 600;
   - `base_ring.png`: the box ring after the boxes settle (frame 900);
   - `base_probe.csv`: positions of the 12 ring boxes every second for 15 s.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T0.1 | `dynamical_tests` runs in CI and has a trivial passing case | exit 0 |
| V0.1 | Capture `base_spawn` twice, then `imgdiff` the two | identical (MAE 0) or ≤ 0.1/255 |
| V0.2 | Capture `base_spawn` and `base_ring` against the goldens just recorded | `imgdiff` thresholds |
| S0.1 | Probe run twice gives identical `base_probe.csv` | max position difference ≤ 1e-6 m |

**Visual confirmation.** In the P0 report:
- `base_spawn` and `base_ring` are correct: terrain, grass, boxes resting on the terrain, the model
  line-up;
- the `imgdiff` heatmap of a deliberately altered capture (camera moved by 0.1 m) visibly fails.

**Exit gate.** CI is green. Goldens and `base_probe.csv` are committed. V0.1 proves captures are
deterministic.

---

## P1 — Library split and scene system (S1)

**Goal.** `dynamical_sim` + `dynamical` + an empty `dynamical_train`. Scenes drive everything, and
`base` is identical to master.

**Steps.**
1. Remove `vulkan/vulkan.h` from `system.h`. Add it where needed.
2. **`TimeSys` goes to the executable unchanged.** It is a tile-map leftover: it scales `dt` by a game
   speed (`time.cpp:53`), uses ImGui and `Input` (`time.cpp:6`, `:24`), and no other system reads
   `Time`. Agent worlds use `WorldClock`. A later cleanup may delete it from `base`.
3. **CMake.** Per-target source lists and the library-boundary moves (architecture.md: `game.*`,
   `scene.cpp` / `scene_spawn.h`, `time.cpp`, `components/input.h`, …), common compile flags,
   `find_package` for glm and Taskflow, `DYNAMICAL_BUILD_VIEWER` / `DYNAMICAL_TRAIN`, the
   `TrainingEnv` output name, resources for both executables, the `ci` and RelWithDebInfo presets.
4. **Header-boundary test** (T1.1).
5. **Scene core:**
   - `scene_def` (nlohmann parse, `include`, variables, substitution, validation with JSON paths,
     resolved dump);
   - `system_registry` / `component_registry` with explicit `registerSimSystems()` /
     `registerViewSystems()`;
   - prefabs.
6. **`SystemSet`:** the untyped `add(list, unique_ptr<System>)`, the fixed lane, and the `Act` /
   `Observe` phases (unit-tested; used from P2).
7. **Renderer pass list from the scene.** `Renderer(reg, RendererDesc)` builds only the listed passes.
8. **`base.scene.json`** reproduces `Game::start`.
   - `SceneSys` splits into the systems `BoxRing` (ring heights come from
     `TerrainGenerator::surfaceHeight`, so it must run its `init` after `ChunkGenerator`'s `preinit`),
     `ModelLineup` (non-recursive over `resources/models`) and `BoxThrower`.
   - `spawnBox` stays view-side for `BoxThrower` and `VRControl` (`vr_control.cpp:101`).
   - `common/desktop.json` supplies `Input, UI, PlayerControl, DebugMenu`. `PlayerControl` creates the
     `Camera` ctx `ClassicRender` needs.
9. `train_main.cpp`: `RunArgs`, resources resolved from the executable directory (no working-directory
   change, no `Settings`), load a scene through `ScenarioRunner`, refuse a renderer block, run
   `mode=bench ticks=N`, exit.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T1.1 | Header boundary: grep `dynamical_sim` sources for `vulkan`, `SDL`, `imgui`, `openxr`, `renderer/` | 0 hits |
| T1.2 | `dynamical_train` import table has no `vulkan-1.dll`, `SDL2.dll` or `openxr_loader.dll` (`dumpbin /dependents`) | none present |
| T1.3 | Scene loader: 15 malformed fixtures (unknown system, bad type, missing variable, cyclic include, renderer in train…). Each fails with the expected file and JSON path | 15/15 |
| T1.4 | Variable precedence: default < `var.x=` on the command line; typed substitution; resolved dump round trip | exact |
| T1.5 | `base` resolves to master's system order, as **registry names** (pre: Input, UI, Time, PlayerControl, VRControl, DebugMenu, ChunkManager, ChunkGenerator, BoxRing, ModelLineup, BoxThrower, Physics; post: Terrain). The `SceneSys` replacements sit where `SceneSys` was | exact |
| T1.7 | `dynamical_train` started from another directory with relative `-envParamsOut=x/y.txt` writes to `cwd/x/y.txt`; `config.6.json` is not created | exact |
| T1.6 | `SystemSet` fixed lane: `Act` runs only at k = 0 and `Observe` only at k = period − 1 over 10 windows | exact call log |
| S1.1 | `base` probe against P0's `base_probe.csv` | max difference ≤ 1e-5 m |
| V1.1 | `base_spawn`, `base_ring` against the P0 goldens | `imgdiff` thresholds, **no re-baseline allowed** |

**Visual confirmation.**
- Interactively: `dynamical` (default scene `base`) behaves as master. You can fly the camera, throw
  boxes, enter VR, and toggle the debug menu.
- The report shows V1.1 next to the P0 goldens.

**Exit gate.** All of the above pass, and T1.2 confirms the headless executable is graphics-free.

---

## P2 — Worlds, Jolt 5.6, flat ground (S2)

**Goal.** N independent physics worlds, stepped in parallel or in real time and drawn. Jolt 5.6.0.

**Steps.**
1. **Jolt 5.6.0 through an overlay port** `ports/joltphysics` (5.6.0, GPU compute options off, no
   patches yet). **No vcpkg baseline bump**: that would also move EnTT, SDL2, OpenXR and tinygltf.
   Rebuild, and run S1.1 and V1.1. Re-baseline with the reason "Jolt 5.6 friction model" only if the
   drift is explained.
2. **`JoltRuntime`** in both mains and in the test main. Remove the registration from `PhysicsWorld`.
3. **`PhysicsConfig`** (threads, sizes, settings). `PhysicsWorld` moves into the registry ctx.
4. **Split `PhysicsSys`:** `RigidBodySys`, `PhysicsStepSys`, `TerrainCollidersSys`, plus the
   accumulator and interpolation system for `base`.
5. **Layers:** `STATIC`, `MOVING`, `AGENT`, `SENSOR`.
6. **Worlds:**
   - `World`, `WorldClock`, `WorldRng`;
   - world templates in scenes;
   - `TrainHost` `mode=bench` stepping worlds with Taskflow;
   - `WorldStepper` (real time) in `dynamical`.
7. **`Ground` component** (box, top at y = 0, friction 1), friction max-combine / restitution
   min-combine callbacks, and `RigidBody::Shape::Capsule`.
8. **`WorldView`** as specified in scenes.md:
   - one host entity per sub-shape;
   - primitives in `resources/models/primitives/` (outside the lineup folder, so `base` is unchanged);
   - capsule meshes generated per (radius, half-height);
   - grid offsets;
   - interpolation from the stepper's alpha.
9. **Test scenes:**
   - `test_boxes` (`dynamical_train`): 25 worlds × 50 boxes dropped on the ground;
   - `test_boxes_view` (`dynamical`): 4 worlds, a fixed camera.
10. Create `docs/physical-agents/benchmarks.md`.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T2.1 | `JoltRuntime`: create and destroy 100 worlds sequentially and 8 concurrently; no leak (allocation counter back to baseline) | exact |
| T2.2 | One world with `threads=1` uses `JobSystemSingleThreaded`; `base` keeps the thread pool | exact |
| S2.1 | `test_boxes` 600 ticks: every box at rest on the ground (y within half-extent ± 2 mm, speed < 1e-3 m/s) | all boxes |
| S2.2 | Determinism: `test_boxes` twice, with 1 thread and with 8 threads, hashing all poses | equal hashes |
| S2.3 | World isolation: worlds with different seeds have different hashes; the same seed in two worlds gives equal per-world hashes | exact |
| S2.4 | Friction combine: a box on a 30° incline with μ = 1 (max with 0.2) stays put; with combine overridden to min, it slides | stays < 1 mm / slides > 0.5 m |
| B2.1 | `mode=bench` ticks/s for 1 and 25 worlds, 1 and N threads, into `benchmarks.md` | recorded, no threshold |
| V2.1 | `test_boxes_view` at frame 300 (golden `worlds_boxes.png`) | `imgdiff` |
| V2.2 | `base_spawn`, `base_ring` after the Jolt bump | `imgdiff`, or a documented re-baseline |
| T2.4 | `base` system list after the `PhysicsSys` split (… `BoxThrower, RigidBody, TerrainColliders, Physics`; post `Terrain`) | exact |
| T2.5 | `WorldView` capsule mesh: generated vertices lie on the capsule surface (distance to the segment − r) | ≤ 1e-4 m |

**Visual confirmation.**
- **Checklist:** 4 separate worlds side by side; boxes rest on each ground; no box crosses into a
  neighbouring world; capsules render with the correct orientation (long axis matches the physics).
- Interactively: pause and single-step `WorldStepper` from the panel.

**Exit gate.** S2.1–S2.4 pass. B2.1 is recorded. The Jolt bump drift, if any, is explained in
`tests/golden/CHANGELOG.md`.

---

## P3 — Bridge skeleton and null environment (M0)

**Goal.** `TrainingEnv.exe mode=null` passes as a Unity build for `mlagents-learn`, and the trainer's
throughput ceiling is measured.

**Steps.**
1. Add `grpc` and `protobuf` to vcpkg. **Vendor the fork's protos** (`protobuf-definitions/proto/mlagents_envs/communicator_objects/*.proto`)
   into `src/mlagents/proto/`, recording the fork commit in `src/mlagents/proto/VERSION`. Generate
   code in CMake.
2. `MlAgentsClient`: connect within 60 s, initialisation exchange, the empty exchange, RESET / STEP /
   QUIT, header 400.
3. Side-channel framing; engine configuration (ignored); environment parameters (float and sampler);
   stats out.
4. `train_args`: the trainer flags, verified against the fork (`environment.py:269-284`):
   `-nographics -batchmode --mlagents-port <p> -logFile <f>`, then `env_args`, including
   `-envParamsOut=`, `-envParams=`, `-gitSha=`, `-set:`.
5. **Logging.** `dy::log` file sink from `-logFile`, flushed per line, plus stdout.
   **CTRL_C** per mlagents-bridge.md "Process behaviour": handler for C, BREAK and CLOSE; gRPC
   deadline 1 s; exit 0 once flagged.
6. **`mode=null`** with the `null` scene (no worlds): 25 agents, a fixed 138 × 2 observation named
   `StackingSensor_size2_VectorSensor_size138`, behaviour `HeidelBotBehavior`, zero reward, episodes
   of 1000 decisions.
7. **`MockTrainer`:** an in-process gRPC server for tests.
8. `tools/train_smoke.py`: runs `mlagents-learn` with `tests/data/null_ppo.yaml` against
   `TrainingEnv.exe`.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| P3.1 | `MockTrainer`: init → empty → RESET → 100 × STEP → QUIT; agent ids stable; done agents receive no action and reappear in the next output; mid-run RESET keeps ids; `base_seed` = the received seed | exact |
| P3.2 | Side channels: byte-exact against blobs captured from Python (`tests/data/sidechannel/*.bin`, produced by `tools/capture_sidechannels.py` with the fork installed) | byte-equal |
| P3.3 | Launch flags parsed, including `-set:x=1` and `-envParams=missing` (exit 1) | exact |
| P3.4 | CTRL_C: launch, send `GenerateConsoleCtrlEvent`, expect exit 0 within 2 s and a log line | exact |
| P3.5 | `envparams.txt` format: header, ordinal sort, round-trip floats, `sha=` present | exact |
| L3.1 | `[slow]` `train_smoke.py`: 2000 steps, exit 0, run directory has events, `Player-0.log` exists | pass |
| B3.1 | Steps/s at 6 and 20 envs, 3 runs each (mean ± sd), into `benchmarks.md` | recorded (**the M0 gate**) |

**Visual confirmation.** `tools/plot_throughput.py` draws steps/s against env count, with the Unity
reference line at 424. The report holds that plot plus a TensorBoard screenshot of the smoke run
(scalars present).

**Exit gate.** P3.1–P3.5 and L3.1 pass, B3.1 is recorded, and a decision note is written: if
throughput stays around 424, engine speed is not the bottleneck (plan D2).

---

## P4 — Exporter, importer, ragdoll at rest (M1)

**Goal.** The noToes body arrives from Unity, is imported into the engine frame, and stands as a
ragdoll that matches Unity's rest pose.

**Steps.**
1. **Unity exporter** (Unity repo, an Editor batch method `JoltExport.Body`). It enters play mode on a
   minimal scene with `HeidelBot.prefab`, waits for `Configure`, and reads back for each segment:
   - pose;
   - mass, centre of mass, inertia tensor and rotation;
   - shapes with transforms and scale;
   - joint data (connected anchors as recomputed at runtime).

   It writes `heidelbot_notoes.body.json` and `heidelbot_notoes_h173.body.json` (root scale
   0.9153439). It also writes:
   - `golden/heidelbot_notoes/rest_unity.png`: a Unity screenshot from a fixed camera (960 × 540, the
     camera pose in the JSON);
   - `rest_pose.json`: world poses for the tests.
2. **Params exporter:** resolved values for `walker.params.json` (from `Training.unity`) and
   `devita_h173.params.json` (from `DeVitaArenaH173.unity`).
3. **C++ Unity importer:**
   - frame mirror;
   - degrees → radians;
   - foot scale baked into shapes; triggers dropped;
   - the knee rest pose moved onto its stop;
   - swing clamp below 180°;
   - masses from percentages.
4. `BodyDef`, `loadBody` validation, `RagdollBuilder` (compound shapes, provided mass properties,
   `OffsetCenterOfMassShape`, `SwingTwistConstraint` limits, group filter, `Discrete` motion quality,
   damping 0, max angular velocity 20).
5. **`rig_anim` scene, rest-only mode** (`common/rig_world.json` without `TrackActions` yet):
   - a kinematic body at rest on the flat ground;
   - `WorldView` draws shapes plus bone axes;
   - `RigPanel` lists segments and joints.
6. **Camera parity.** `rig_rest.capture.json` uses the exporter's camera pose, converted to the
   engine frame.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T4.1 | `[import]` right-handed triad after conversion for every segment frame | `cross(x', y') · z' > 0.9999` |
| T4.2 | Imported rest pose **before the knee move** against `rest_pose.json` (converted); after the move only the knee-descendant chain differs, by the 2.1° rotation | ≤ 1e-5 m, ≤ 1e-4 rad |
| T4.3 | Joint anchors coincide in world space at rest (parent side against child side) | ≤ 1e-5 m |
| T4.4 | Masses: percentage × total; sum 99.98 % ± 0.05; H173 masses equal the unscaled body's at 69 kg | ≤ 1e-6 kg |
| T4.5 | Jolt mass properties equal the file (mass, centre of mass, inertia after rotation) | relative 1e-5 |
| T4.6 | Self-collision: every segment pair is filtered (a collide-shape query between all pairs returns nothing) | 0 contacts |
| T4.7 | Validation: 10 malformed body fixtures are rejected with messages | 10/10 |
| S4.1 | Dynamic ragdoll at rest with gravity 0, drives off, 1000 ticks: the pose stays put | drift ≤ 1e-5 m |
| S4.2 | Kinematic pelvis, passive joints (no motors), gravity on, 300 ticks: the limbs hang within the limits and settle without exploding | no NaN; max excursion ≤ 1°; at rest within 3 s |
| V4.1 | `rig_rest` capture (golden `rig_rest.png`) | `imgdiff` |

**Visual confirmation.** The report shows `rig_rest.png` beside `rest_unity.png`, taken from the same
camera.

Checklist:
- the silhouette and proportions match;
- the left arm is on the screen side where Unity shows it (no mirroring);
- feet are flat on the ground and the arm asymmetry is visible;
- the knees show the 2° bend;
- the bone-axis gizmos are right-handed (red x, green y, blue z).

**Exit gate.** T4.* and S4.* pass, and the side-by-side is signed off.

---

## P5 — `rig_anim` track playback (M1)

**Goal.** Unity pose tracks play on the kinematic rig. Limits, bone lengths, velocities and ground
contact are checked live and in CI.

**Steps.**
1. **Unity pose-track bake** (`JoltExport.Tracks`). For each controller (BasicMotions@Walk at 0.67;
   Walk1p500mps-h173 at 0.807316; Walk1p500mps), record the world poses of the 16 bodies per physics
   tick. Also record every 30th frame as a Unity screenshot from the track camera into
   `golden/<body>/<track>/unity_<frame>.png`.
2. `PoseTrack` loader (dt within a relative 1e-5); `TrackActions` (lib; `MoveKinematic` and teleport
   modes, transport controls); `BodySync` (anchor finite differences, joint angles); `RigChecks`
   (lib; geometric ground check); `rig_report.json`. Kinematic ragdolls set
   `mCollideKinematicVsNonDynamic`.
3. **Rendering:** joint arcs with limit wedges (green inside, red outside); foot-contact markers.
4. The `rig_check` scene (`common/rig_world.json` only) runs in `dynamical_tests` through
   `ScenarioRunner`, headless.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| S5.1 | Track header against body: name, sha, segment count, dt | exact or refused |
| S5.2 | Bone lengths constant over every track | ≤ 1e-4 m |
| S5.3 | Joint limits: per joint, the worst excursion past each limit over the track, written to `rig_report.json` and compared with `tests/data/rig_excursions.json` (expected values reviewed once) | per joint, ≤ expected + 0.5° |
| S5.4 | Velocity continuity: anchor velocities have no spike (the maximum frame-to-frame Δv is at most 5 × the 99th percentile), including the loop seam | pass |
| S5.5 | Ground: the lowest foot point per frame | penetration ≤ 5 mm, and float during stance ≤ 10 mm |
| S5.6 | `MoveKinematic` and teleport modes give the same poses one tick apart | ≤ 1e-5 m |
| V5.1 | `rig_walk` capture script: 8 frames across one gait cycle from a side camera (goldens `rig_walk_<i>.png`) | `imgdiff` |

**Visual confirmation.** The report holds a contact sheet of the 8 engine frames above the 8 Unity
screenshots of the same ticks.

Checklist:
- the gait reads as walking, with the same phase in both rows;
- feet plant on the ground and do not slide during stance;
- limit wedges turn red only where `rig_report.json` lists excursions (the demos exceed the stops, as
  in Unity);
- the root advances toward −z in the engine.

Also a 10 s MP4 (`capture` with every frame, encoded by ffmpeg if present).

**Exit gate.** S5.* pass. The excursion baseline is reviewed and committed. The contact sheet is signed
off.

---

## P6 — Joint limits and drives (M2)

**Goal.** Limits match Unity's envelope, and drives behave as a spring–damper toward a moving target
(`PositionAndVelocity`), with the gain scale derived.

**Steps.**
1. **Unity limit-sweep rig** (`JoltExport.LimitProbes`): for each joint, drive the child to each limit
   (twist low/high, swing Y±, swing Z±, plus 8 cone diagonals) and record the child's local rotation.
   Write the result into the body file's `limit_probes`.
2. **Unity step-response recordings** (`JoltExport.DriveSteps`): one joint at a time, with the rest of
   the body kinematic, at gains (0.5, 0.5) and (1, 1). Record the angle against time for a 20° twist
   step and a 20° swing step. Write `golden/<body>/drive_<joint>_<case>.csv`.
3. **Motors.** `HeldDrive`, `DriveTargets`: `EMotorState::PositionAndVelocity`; stiffness and damping
   from the actions × factors × `drive_gain_scale`; torque limit; target orientation `swing · twist`
   relative to rest; target angular velocity.
4. **`joint_lab` scene:**
   - **sweep mode:** 15 worlds, one joint each, the parent fixed and the child sweeping;
   - **step mode:** step responses;
   - **ramp mode:** a constant ω target.
5. `tools/fit_gain_scale.py` fits `drive_gain_scale` (one scalar, or one per joint group if needed)
   to minimise the step-response error against the Unity CSVs. The result goes into the params file.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T6.1 | `[limits]` each probe lands on the Jolt limit (the constraint reports the limit active, and the angle error to the limit surface) | ≤ 1° for all probes |
| T6.2 | Twist sign after reflection: knee low stop 2° and ankle low −38°, approached from inside | the limit holds at the right side |
| T6.3 | Action mapping: a = 0 gives mid-twist (knee 67.31°); a = ±1 gives the twist limits; ay = ±1 gives ±Y | ≤ 1e-4 rad |
| T6.4 | `PositionAndVelocity` ramp: a constant ω target of 1.0472 rad/s on a free joint | steady-state velocity error ≤ 2 % |
| T6.5 | Zero spring, damping only (s = −1, d = 1): damps toward the ω target, not toward 0 | sign and magnitude ≤ 5 % |
| S6.1 | Random actions for 60 s on a full dynamic ragdoll lying on the ground | limit excursion ≤ 1° at all times; no NaN |
| S6.2 | Step responses after fitting, against Unity: rise time, overshoot, settling time | each within 15 % of Unity, or a documented exception per joint |
| S6.3 | Passive drop: ragdoll from 1 m, drives off | no NaN; max segment speed < 15 m/s; at rest in 3 s |
| S6.4 | Standing: dynamic ragdoll at rest, gravity on, motors holding rest at gains (1, 1) × scale, 10 s | stands; pelvis drift ≤ 5 cm; max segment speed < 0.5 m/s after 1 s |
| V6.1 | `joint_lab` sweep capture (golden `joint_sweep.png`) | `imgdiff` |

**Visual confirmation.**
- **Sweep grid:** each joint's wedge and its child reaching the wedge edges, and nothing poking
  through.
- **Plots:** Jolt against Unity step responses per joint (`drive_steps.png`); the ramp-tracking plot.
- **Checklist:** knee and ankle stops visibly asymmetric as in Unity; forearm twist axis correct on
  both sides; the head does not reach its 90° limit in normal motion.

**Exit gate.** T6.* and S6.* pass. The gain scale and its fit report are committed. The plots are
signed off.

---

## P7 — Contacts, friction, exact energy (M2)

**Goal.** Contact wrenches per foot and exact joint work per sub-step, with books that close.

**Steps.**
1. **Overlay patches on Jolt 5.6.0:**
   - P-A: limit-axis accessors;
   - P-C: a post-solve contact impulse listener with the 5.6 per-manifold friction layout
     (jolt-readouts.md §0, §3).

   Each patch has its own unit test in our tree.
2. Own sub-stepping in `PhysicsStepSys` (`Update(h_sub, 1)` × n).
3. **`Contacts`:** P-C impulses per segment and foot (normal, friction, torsional), contact point,
   centre of pressure; the momentum-balance cross-check.
4. **`Energy`:**
   - per sub-step motor, limit and anchor work from lambdas;
   - the τ / ω_rel vectors per tick;
   - whole-body E, dE, the closure residual;
   - clamp-event and CCD counters.
5. Append `Contacts` to `common/rig_world.json` (contact markers in `rig_anim`).
6. **`physics_lab` scene:**
   - a passive drop;
   - a quiet stance held by the motors;
   - a slide test (a box and a foot on the ground with an applied force);
   - a pendulum (one segment on one joint).

   It draws ground-reaction arrows, the centre of pressure and per-joint power bars.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T7.1 | P-C impulses against the momentum balance on a single box resting, sliding and spinning on the ground | ≤ 1e-4 relative |
| T7.2 | P-A limit axes against axes reconstructed from the constraint rotation | ≤ 1e-5 |
| S7.1 | Free fall, no drives, no contact, 2 s: KE + PE constant | ≤ 0.1 % |
| S7.2 | Passive pendulum, damping 0: energy constant | ≤ 0.5 % over 10 s |
| S7.3 | Flight phase: contact work | 0 to 1e-6 J |
| S7.4 | Quiet stance 10 s: Σ vertical ground-reaction force = m·g | ≤ 0.5 % mean, ≤ 2 % per tick |
| S7.5 | Closure: `dE − (Σ motor + Σ limit + contact + gravity)` on 60 s random-action rollouts, flat ground | ≤ 2 % of Σ\|work\| |
| S7.6 | Slide: friction force = μ·N until stiction is broken (μ = 1, max combine) | ≤ 2 % |
| S7.7 | Velocity clamp: rollouts at standard gains log clamp events | 0 events, or listed per joint |
| V7.1 | `physics_lab` stance capture with arrows (golden `lab_stance.png`) | `imgdiff` |

**Visual confirmation.**
- **Plots:** the energy-closure time series (`energy_closure.png`); stance ground-reaction force
  against m·g; the slide friction curve.
- **Captures:**
  - stance: arrows vertical, centre of pressure under the feet;
  - drop: arrows only during contact.
- **Checklist:** no arrow appears in flight; the centre-of-pressure marker stays inside the foot boxes
  in quiet stance.

**Exit gate.** S7.* pass, the patches are committed with tests, and the plots are signed off.

---

## P8 — Agent runtime and Unity oracle (M3)

**Goal.** The task (`heidelbot_notoes_2`) is complete. Unity's math reproduced by the `_1` oracle proves
the rig, frames and features against Unity logs.

**Steps.**
1. **Unity golden logger** (`JoltExport.Rollouts`): 20 episodes, scripted plus random actions, on both
   lines. Output `golden/<body>/rollout_<n>.json` (LFS if needed), schema in
   [assets.md](assets.md#golden-rollout-files).
2. **Layouts:** `heidelbot_notoes_1` (oracle: non-orthogonal referential, right-side negation, ZXY
   targets) and `heidelbot_notoes_2` (agent-runtime.md). Element names for both.
3. **Systems:** `TrainerActions`, `Disturbances`, `Task`, `Rewards`, `Termination`, `Observation`,
   `Episode`, `Stats`, and the `Outbox`. Episode parking for a kill mid-window.
4. **Reset order** with `env_target_speed_overrides_draw`; `AnchorHistory` reset after the pose writes.
5. **`rig_anim` observation panel:** the live `_2` vector with names, and the `_1`-against-golden diff
   heatmap when a rollout is loaded.
6. **`walker_heuristic` scene:** `humanbot_world` with `HeuristicActions` (random or zero), plus
   `WorldView` and a first `AgentPanel` (episode, rewards, joints). `walker_view` reuses both in P11.
7. Append `Observation@observe` to `common/rig_world.json` and add the observation panel.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| S8.1 | `[oracle]` the `_1` observations on Unity rollout poses (imported) against the Unity log | ≤ 1e-4 per element; the failing index printed with its name |
| S8.2 | `[oracle]` reward terms (root, death) and kill or truncation ticks against the log | reward ≤ 1e-4 relative; events exact |
| T8.1 | `_2` heading frames orthogonal and yaw-only for 1000 random poses, including near-vertical segments (fallback path) | `\|H·Hᵀ − I\|` ≤ 1e-6; `H.y = up`; no NaN |
| T8.2 | Shared quantities, `_2` against `_1`: head error equal; header z equal up to its documented sign flip; joint axes equal up to the frame change and the right-side negation; anchor velocities equal before projection | ≤ 1e-5 |
| T8.3 | Reset spike: after random yaw and random init, the first anchor velocity | ≤ 5 m/s (Unity: 60–120) |
| T8.4 | Kill threshold depends only on `dyingHeightFactor` and the rest pose (moving the spawn pose does not change `est0`) | exact |
| T8.5 | Reset order, first-decision skip, terminal-window reward counted, `env_target_speed_overrides_draw` true and false | exact |
| T8.6 | Window parking: a kill at tick k of a window gives a terminal step at the window end with the snapshot at tick k; other worlds unaffected | exact |
| T8.7 | Unity's stat keys (`tests/data/stat_keys_walker.txt`, from a Unity run) ⊆ the emitted keys; the extras are exactly the documented list | subset + exact extras |
| S8.3 | Heuristic random actions, 25 worlds, 10 min simulated: no NaN, kills happen, every episode resets cleanly, ids stable | pass |

**Visual confirmation.**
- **Captures:** the `rig_anim` observation-diff heatmap (all green against the golden);
  `walker_heuristic` showing a kill, the reset and the next episode (contact sheet of 6 frames,
  golden).
- **Plot:** episode-length histogram from S8.3.
- **Checklist:** after reset, the agent stands at rest facing the target; a push visibly hits one
  segment; nothing teleports mid-episode.

**Exit gate.** S8.1 and S8.2 are green on every golden rollout. T8.* pass. The visuals are signed off.

---

## P9 — Demo recorder (M5)

**Goal.** `.demo` files from pose tracks that GAIL accepts, cached by content.

**Steps.**
1. `mode=record-demo`:
   - a kinematic, non-colliding body; `TrackActions`;
   - `Recording` (header x,z = 0);
   - 1000-tick episodes, zero actions;
   - 3000 decisions for the walker, 10000 for DeVita.
2. `DemoWriter` (meta, brain parameters, info/action pairs), checked against the fork's `demo_loader.py`.
3. Demo cache key and the `Demonstrations/` layout. Reusing the cache from `start-run` comes with its
   `-Engine dynamical` path in P10 (today `quick-train.ps1` wipes `Demonstrations/`).

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T9.1 | Round trip: write, then read with our reader | exact |
| L9.1 | `[slow]` the fork's `demo_loader.load_demonstration` reads the file; the spec matches the brain | pass |
| T9.2 | Every recorded header has x = z = 0, and y equals the head error | exact |
| T9.3 | Cache: the same inputs reuse the file; changing any key input regenerates it | exact |
| S9.1 | Demo observations against a Unity demo of the same clip (`_1` oracle layout only): per-channel mean and std over the file | ≤ 5 % per channel, with differences explained (one-tick `MoveKinematic` shift) |

**Visual confirmation.**
- **Plot:** per-channel overlays, Unity demo against engine demo (`_1`), for the joint-velocity
  channels of both legs.
- **Capture:** `rig_anim` playing the demo track with the observation panel open.

**Exit gate.** L9.1 and T9.* pass, and the overlay plot is signed off.

---

## P10 — Full bridge and real training runs (M4)

**Goal.** An unmodified Unity config trains on `TrainingEnv.exe`, and `start-run` / `stop-run` work.

**Steps.**
1. The bridge gathers outboxes from N worlds in world order, scatters actions per behaviour, sends
   brain parameters with the first info, and handles NaN.
2. **Env params:** the full accepted set (mlagents-bridge.md) with precedence, plus a warning on
   unknown names; `envparams.txt` with sha, body, params and layout hashes.
3. **`start-run` gains an `-Engine dynamical` path** (Unity repo scripts):
   - skip the Unity build;
   - record or reuse demos with `TrainingEnv.exe mode=record-demo` and the cache, instead of the
     Editor recorder;
   - point `--env` at `TrainingEnv.exe`;
   - keep the run naming, port window, health grep and output lines.
4. Stats: Unity's keys plus the documented extras.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| P10.1 | `MockTrainer` with real worlds: 25 agents, 10 000 steps, random actions; ids stable; done agents routed; RESET mid-run | exact |
| P10.2 | Env-param precedence (`-set` > live > file > params); unknown name warned once; layout-defining name rejected | exact |
| L10.1 | `[slow]` **unmodified** `default-training.yaml` (GAIL on, demos from P9) for 1M steps on 4 envs via `start-run -Engine dynamical` | exit 0; ready marker reached; `stop-run` exports ONNX; Unity's key set (`tests/data/wandb_keys_walker.txt`) ⊆ engine keys |
| L10.2 | `[slow]` a curriculum lesson change (test config) triggers RESET and the run continues | pass |
| B10.1 | Decisions/s per process at 25 worlds, against the Unity build at the same config | recorded |

**Visual confirmation.**
- **Plot:** episode length and root speed against steps for the 1M-step run (TensorBoard export to
  PNG), beside the same window of the frozen Unity reference run.
- **Checklist:** the curves move; no NaN; the episode length rises above the random-action baseline.

**Exit gate.** L10.1 and L10.2 pass, B10.1 is recorded, and the plot is signed off.

---

## P11 — ONNX inference and `walker_view` (M5b)

**Goal.** Policies run in the engine, and the viewer follows a live run.

**Steps.**
1. Add `onnxruntime` (vcpkg, or the prebuilt fallback); `PolicyRunner`, with load-time validation
   against the layout. Depends on P8 (layout) and an exported ONNX (P10).
1b. **Minimal `mode=eval`** (`walker_eval` scene): N episodes, deterministic actions through a
    `PolicyHandle`, summary JSON (episode length, dead rate, speed). CSV and `EVT|` parity with Unity
    come in P14.
2. `PolicySys` (batched, deterministic); a `latest:` model watcher with safe file detection and a swap
   at episode start.
3. **`walker_view` scene:** extends `walker_heuristic` (P8) with `Policy`, `ModelWatcher`, a target you
   can place, a speed slider, the full `AgentPanel` (joints, rewards, energy, observations, physics),
   and a checkpoint name and step overlay.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T11.1 | `PolicyRunner` against Python onnxruntime on 100 recorded observations (`tools/onnx_reference.py`) | ≤ 1e-5 |
| T11.2 | Layout mismatch (wrong obs size, recurrent model) refused | exact |
| T11.3 | Watcher: a half-written file is ignored; the new checkpoint swaps at the next episode start; no frame takes longer than 50 ms during the swap | pass |
| S11.1 | Minimal `mode=eval`: the policy from L10.1 over 20 episodes beats random actions on mean episode length | > random + 3 sd |

**Visual confirmation.**
- **Captures:** `walker_view` running the L10.1 checkpoint (golden contact sheet of 6 frames,
  fixed seed); the panel screenshot.
- **Live check:** during a training run, the checkpoint label changes within one episode after
  export.
- **Checklist:** the walker faces and moves toward the placed target (if the policy has learned
  anything); the overlay shows the right run and step.

**Exit gate.** T11.* and S11.1 pass, and the live check is signed off.

---

## P12 — First GAIL walker (M6)

**Goal.** The flat GAIL walker trained from scratch on `_2` reaches the frozen Unity reference.

**Steps.**
1. Demos from P10 for `BasicMotions@Walk`.
2. `default-training.yaml` unchanged, with GAIL on; 40M steps; 3 seeds.
3. `tools/compare_runs.py`: behavioural stats at matched gradient updates against the frozen Unity run
   from P3. Stats: dead rate, episode length, root speed against target, head height.
4. Gain-scale iteration only if the gate fails and the gait diagnosis points at the drives.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| L12.1 | Each seed completes 40M steps without NaN or crash | 3/3 |
| L12.2 | `compare_runs.py` on matched updates: each stat within the Unity reference's seed spread, or better, at 20M and 40M | pass |
| S12.1 | `mode=eval` of the final checkpoint: 100 episodes at target speeds {0, 1.36} | dead rate ≤ Unity's + 5 pp |

**Visual confirmation.**
- **Video:** `walker_view`, 3 episodes per target speed, side camera and follow camera; Unity
  reference video beside it.
- **Plot:** gait cycle (joint angles of hip, knee and ankle over a normalised stride), engine against
  Unity.
- **Paul's judgement** is recorded in `reports/P12.md`.

**Exit gate.** L12.2 and S12.1 pass, and Paul signs off the video.

---

## P13 — DeVita energetics and slope (M7)

**Goal.** The DeVita H173 line runs on the exact energy channel, with the slope ground.

**Steps.**
1. `devita_h173.params.json` and the H173 body; behaviour `AnySpeedBotBehavior`; demos from
   `Walk1p500mps-h173`.
2. **Energy reward** on channel 3 (UA §4.1). `energyScale` and multipliers re-derived for g = 9.81
   and the exact channel (`tools/derive_energy_scale.py`, from eval rollouts of a reference
   checkpoint).
3. `DeVitaStepStat` (stance detection, sagittal work, ratio of sums) on the exact per-joint vectors.
4. **`SlopeGround`:** rotation about the medio-lateral axis, drop, speed compensation; the env params.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T13.1 | `DeVitaStepStat` on a synthetic gait (scripted torques and velocities with known stance windows) | stance windows exact; work ≤ 1e-6 J; ratio exact |
| T13.2 | Stance detector on **dynamic** rollouts of the P12 walker, against geometric foot contact (kinematic tracks have no impulses) | stance windows within ± 2 ticks of the geometric contact |
| T13.3 | Slope geometry: 10° rotation; drop = footReach·tan θ + margin; speed compensation cos θ | ≤ 1e-6 |
| S13.1 | Energy reward on a fixed rollout equals a Python re-implementation of UA §4.1 | ≤ 1e-5 relative |
| L13.1 | `[slow]` one DeVita config (equal multipliers) for 20M steps | completes; `DeVita/*` and `EnergyProvider/*` keys present |

**Visual confirmation.**
- **Captures:** the slope arena at ±10° with the walker (golden).
- **Plots:** per-stance hip, knee and ankle sagittal power curves; the neg/pos ratio over training
  against the Unity DeVita runs (a finding, not a gate).
- **Checklist:** the slope reads correctly in the render; the stance windows on the plot match the foot
  contacts in the video.

**Exit gate.** T13.*, S13.1 and L13.1 pass, and the plots are signed off.

---

## P14 — Eval mode and replays (M8)

**Goal.** Evals and videos are produced with no Unity build.

**Steps.**
1. `mode=eval` with the same CSV columns and `EVT|` lines as Unity's energy-eval and gait-cycle
   builds (schema from the Unity repo's `energy_eval_run.py` / `gait_run.py`).
2. **Replay writer** (`.dyrep`: header plus per-tick poses, per-decision actions, rewards, contacts,
   energy); `docs/physical-agents/replay-format.md`.
3. Replay playback in `walker_view`, with a scrubber and synced plots.
4. A Unity `ReplayPlayer` scene (Unity repo) for skinned publication videos.

**Automated tests.**

| ID | Test | Threshold |
|---|---|---|
| T14.1 | Eval CSV header equals the Unity schema | exact |
| L14.1 | `[slow]` `energy_eval_run.py` and `gait_run.py` run unchanged on engine output | exit 0 |
| T14.2 | Replay round trip: replayed poses equal the live poses | bit-exact |
| T14.3 | Replay format version check; truncated file rejected | exact |

**Visual confirmation.**
- **Captures:** a replay scrubbed to three timestamps, matching the live captures at the same ticks
  (`imgdiff` live against replay).
- **Video:** the Unity `ReplayPlayer` video of the same episode.

**Exit gate.** L14.1 and T14.* pass, and the videos are signed off.

---

## P15 — Extended lines (M9)

Each item follows the P8 + P12 pattern (oracle where Unity data exists, behavioural gate, video
sign-off), in this order:
1. Lean line (slope from the first episode; demos from the SlopeGait lean set).
2. Body variants.
3. Strike.
4. Duel (self-play, teams).
5. Heightfield terrain.
6. Policy walkers in `base` on marching-cubes colliders, using the engine frame directly.

**Visual confirmation per item:** a scene capture, plus a video beside Unity's.
