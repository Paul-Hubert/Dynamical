# Game, worlds and the system set

`Game` in [`src/view/game.cpp`](../../../src/view/game.cpp), `SystemSet` in [`src/logic/systems/system_set.cpp`](../../../src/logic/systems/system_set.cpp), `World` / `Worlds` in [`src/scene/world.cpp`](../../../src/scene/world.cpp), `ScenarioRunner` in [`src/scene/scenario_runner.cpp`](../../../src/scene/scenario_runner.cpp), `Probe` in [`src/scene/probe.cpp`](../../../src/scene/probe.cpp) · Scope: Host (Game, Probe), World (World), Both (SystemSet) · Needs renderer: Game only
· Lanes: none (they run the lanes) · Parameters: command line (`RunArgs`, `Settings`), world templates

The objects that own the registries and run the systems: `Game` is the process of `dynamical` (main registry,
renderer, frame loop); `World` is one independent simulation (its own registry, Jolt system, clock, RNG and fixed
lane); `Worlds` is the list of a scene's worlds; `ScenarioRunner` loads a scene and steps its worlds headless
(tests, `dynamical_train`); `SystemSet` holds the systems of a registry by lane and calls them; `Probe` writes the
poses of the host's bodies to a CSV. The entry points are [`src/main.cpp`](../../../src/main.cpp) (`dynamical`) and
[`src/train_main.cpp`](../../../src/train_main.cpp) (`dynamical_train`). The view folder (cameras, capture,
WorldStepper, WorldView) is documented by its own pages; this page only says what these objects put in the
registries and how they run systems.

## Behaviour

### main and train_main

- `main` (`dynamical`): `profile=true` starts Tracy; moves to the closest parent of the executable holding a
  `resources` folder when `./resources` is missing; makes the process-wide `JoltRuntime`; registers the library's
  systems and components (`registerSimSystems`), the view's (`registerViewSystems`) and the policies' (with ONNX);
  builds a `Game` and calls `start()`.
- `train_main` (`dynamical_train`, `TrainingEnv.exe`): parses `TrainArgs` and `RunArgs` (any unknown key is an
  error), optional log file, `JoltRuntime`, `registerSimSystems` (+ policies); loads the scene (`scene=`, else
  `null` for `mode=null`, else `walker_train`) from `resources/` next to the executable with
  `allow_renderer = false` into a `ScenarioRunner`; writes `scene.resolved.json` next to the log file; runs
  `TrainHost`. It never changes the working directory and never touches `config.7.json`.

### Game (host registry)

- **constructor**: emplaces `Settings` (reads and rewrites `./config.7.json`, then the `key=value` overrides),
  a `tf::Executor` of `hardware_concurrency` named workers, `RunArgs` (unknown keys that are not `Settings` keys are
  logged as warnings). Deterministic runs (`fixed_frame_dt > 0`: captures, probes) get a 960 × 540 window (unless
  `gpu_times` or `capture_window`), no fullscreen, `vr_mode` 0. Emplaces `Graphics`: the levels of
  `resources/graphics.json` and the current one (`Settings::graphics`, cleared in deterministic runs unless
  `graphics=` is given); the level sets `view_distance` and `RunArgs::ray_tracing` unless given on the command line.
  Emplaces `FrameClock`.
- **start**: loads the scene (`scene=`, the capture script's scene, else `base`); a scene without a `renderer`
  block is refused. `vr_mode` = 0 when the renderer block says `vr: false`. Makes the `Renderer`, then a `SystemSet`
  with the scene's `host.pre` and `host.post` systems (by registry name, with their params). When the scene has
  worlds, emplaces `Worlds` and creates them (base seed `RunArgs::seed`). Then **preinit**: renderer, host systems,
  worlds; **init**: host systems, worlds, renderer. Optional `Capture` and `Probe`.
- **frame loop**: `renderer->prepare()`; `Input::EXIT` ends the loop (and is cleared); `dt` from the wall clock, or
  `RunArgs::fixed_frame_dt`; `FrameClock` (`dt`, `time += dt`, `frame++`); `SystemSet::pre_tick(dt)`,
  `post_tick(dt)`; `Probe::sample`; capture request; `renderer->render()`; capture readback and save; exit after
  the capture or after `RunArgs::ticks` frames; `DY_FRAME()`.
  The worlds are not ticked here: `WorldStepper`, a host system, steps them.
- **destructor**: `Worlds::finish`, `SystemSet::finish`, `renderer->finish`; the system set is destroyed before the
  renderer (systems own GPU resources).

### SystemSet

- Three lists: `Pre`, `Post` (host, once per frame) and `Fixed` (a world, once per fixed tick). `add(list, system,
  phase, name)`: the name is the registry name of the scene (`System::name()` when empty), used for the profiler
  zones (`<name>` around a tick, `preinit <name>`, `init <name>`, `finish <name>`).
- `preinit()`, `init()`, `finish()`: every system of `Pre`, then `Post`, then `Fixed`, in insertion (scene) order.
- `pre_tick(dt)`, `post_tick(dt)`: the systems of that list in order.
- `fixed_tick(h, act, observe)`: `Every` systems always, `Act` systems (`@act`) only when `act`, `Observe`
  (`@observe`) only when `observe`; when the registry's `Parked` flag is set only the `Observe` systems run.
  `fixed_observe_only()`: the `Observe` systems with `h = 0` (the trainer's RESET).

### World and Worlds

- **World constructor** (`index`, `seed`, template): emplaces in the world's registry `PhysicsWorld` (from the
  template's `physics` block: `threads`, `max_bodies`, `max_body_pairs`, `max_contacts`, `temp_bytes`, `gravity`,
  `substeps`, `allow_sleeping`, `friction_combine`, `restitution_combine`, `velocity_steps`, `position_steps`,
  `speculative_distance`, `penetration_slop`), `WorldClock` (`clock` block: `physics_hz` 60, `control_hz` = physics
  by default, a divisor of it), `WorldRng(seed)`, `WorldInfo`, `Parked`; adds the template's `fixed` systems.
- **preinit**: the systems' preinit. **init**: creates the template's entities (every component through its
  registered JSON emplacer: `Transform`, `RigidBody`, `Ground`, `HeightField`, `PointLight`, `ModelRef` and the
  other registered ones), then the systems' init.
- **tick**: `k = WorldClock::tick % decision_period`; `fixed_tick(dt, k == 0, k == period - 1)`; `tick++`.
- **finish** (once, also from the destructor); **hash**: FNV-1a of the poses of every created `RigidBody` body, in
  entity order (determinism tests).
- `worldSeed(base, index)` = splitmix64 mix: each world its own stream. `Worlds::create` makes `count` worlds of
  each world entry, indices running on; `Worlds::step(ticks, executor)` ticks every world `ticks` times, one taskflow
  task per world when there is an executor and more than one world.

### ScenarioRunner

- Constructor: `loadScene`, `Worlds::create(base_seed)`, preinit, init (renderer and host blocks ignored).
  `restart(seed)`: finish, recreate, preinit, init. `step(ticks, executor)`: `Worlds::step`, `DY_FRAME()` (a frame of
  the profiler without a window). Destructor: finish.

### Probe

- `probe=<file.csv> probe_entities=RigidBody|Transform probe_every=60`: every `probe_every` frames, one row per
  entity with `Transform` (+ `RigidBody`), in entity order: `frame,index,entity,x,y,z,qw,qx,qy,qz`.

## Parameters

Command line keys of `RunArgs` (`scene`, `var.<name>`, `mode`, `threads`, `ticks`, `episodes`, `seed`, `profile`,
`capture`, `capture_dir`, `fixed_frame_dt`, `window_hidden`, `exit_after_capture`, `gpu_times`, `capture_window`,
`ray_tracing`, `lighting`, `probe_rays`, `far_plants`, `probe`, `probe_entities`, `probe_every`) and of `Settings`
(`window_width`, `window_height`, `fullscreen`, `fps_max`, `username`, `server_side`, `client_side`, `vr_mode`,
`view_distance`, `graphics`, `grass_kind`, `ground_relief`); see their component pages. World templates: the
`physics` and `clock` blocks above.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Settings](../components/Settings.md) | context | readwrite+create | `Game::Game`, `Game::start` | emplaced from config file + arguments; window size, fullscreen, `vr_mode`, `graphics`, `view_distance` adjusted |
| [Executor](../components/Executor.md) | context | write+create | `Game::Game` | the host's `tf::Executor` (chunks, colliders, recording, worlds) |
| [RunArgs](../components/RunArgs.md) | context | readwrite+create | `Game::Game`, `Game::start` | parsed arguments; `ray_tracing` set by the graphics level |
| [Graphics](../components/Graphics.md) | context | write+create | `Game::Game` | levels of `resources/graphics.json` and the current one |
| [FrameClock](../components/FrameClock.md) | context | readwrite+create | `Game::Game`, frame loop | `dt`, `time`, `frame` written before the systems tick |
| [Worlds](../components/Worlds.md) | context | write+create | `Game::start`, `Game::~Game` | the scene's worlds, for `WorldStepper` and `WorldView`; finished in the destructor |
| [Input](../components/Input.md) | context | readwrite | frame loop | `on[EXIT]` read and cleared |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | write+create | `World::World`; read in `World::hash` | the world's Jolt system from its template |
| [WorldClock](../components/WorldClock.md) | context | readwrite+create | `World::World`, `World::tick` | fixed tick and decision window; `tick` advanced |
| [WorldRng](../components/WorldRng.md) | context | write+create | `World::World` | the world's random stream |
| [WorldInfo](../components/WorldInfo.md) | context | write+create | `World::World` | index, seed, template name |
| [Parked](../components/Parked.md) | context | write+create | `World::World`; read in `SystemSet::fixed_tick` | only Observe systems run while set |
| [RigidBody](../components/RigidBody.md) | component | read | `World::hash`, `Probe::sample` | bodies to hash or probe |
| [Transform](../components/Transform.md) | component | read | `Probe::sample` | poses written to the CSV |

## Side effects

- Reads and writes `./config.7.json` (`Settings`, dynamical only); reads `resources/graphics.json`, the scene files
  and prefabs; writes the probe CSV, the captures (view's `Capture`), `scene.resolved.json` (train).
- Creates the `Renderer` (Vulkan, SDL window, OpenXR) in `Game::start`.
- Creates a thread pool (`tf::Executor`), and one Jolt job system per `PhysicsWorld`.
- Creates the template entities of every world in `World::init`.
- Log: unknown arguments, ignored values, unknown graphics level, `Graphics : <name>`, scene errors.
- Profiler zones `load scene`, `create renderer`, `preinit`, `init`, `probe`, `capture`, `World preinit`, `World
  init`, `World tick`, `Step worlds`, one per system call; frame marks.

## Order and dependencies

- Host: renderer constructor and preinit → host systems preinit → worlds preinit; host init → worlds init →
  renderer init. The renderer emplaces `RenderSettings`, `VRRig`, `VRInput` before the systems' preinit; `Input`
  (the `Input` system) and `Camera` (`PlayerControl`) come from host systems, so `Game::start` needs a scene with
  `common/desktop.json` (the loader enforces it for rendered scenes).
- A world's context (`PhysicsWorld`, `WorldClock`, `WorldRng`, `WorldInfo`, `Parked`) exists before any of its
  systems is constructed.
- `JoltRuntime` must outlive every `PhysicsWorld` (it lives in main).

## Used in scenes
<!-- generated -->

## Tests

- `T1.6 SystemSet fixed lane: ...` (two cases), `SystemSet keeps registry names and master's typed adds` ([`tests/unit/system_set.cpp`](../../../tests/unit/system_set.cpp)).
- `Worlds: clock and phases and physics and seeds from the template`, `WorldRng: the same seed gives the same stream ...` ([`tests/unit/world.cpp`](../../../tests/unit/world.cpp)).
- `S2.1`, `S2.2`, `S2.3` ([`tests/unit/test_boxes.cpp`](../../../tests/unit/test_boxes.cpp)): `ScenarioRunner`, `World::hash`.
- `RunArgs ...` ([`tests/unit/run_args.cpp`](../../../tests/unit/run_args.cpp)), `GRAPHICS.*` ([`tests/unit/graphics.cpp`](../../../tests/unit/graphics.cpp)).
- `T2.1`, `T2.2` ([`tests/unit/jolt_runtime.cpp`](../../../tests/unit/jolt_runtime.cpp)): `JoltRuntime` and many worlds.
- Probe: `S0.1`, `S1.1` ([`CMakeLists.txt`](../../../CMakeLists.txt), [`tests/golden/base_probe.csv`](../../../tests/golden/base_probe.csv)); `T1.7` (train from another directory).
