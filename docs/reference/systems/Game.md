# Game

> **Kind:** service · **Registry name:** — · **Scope:** n/a · **Lane:** per-frame loop owner, called from `main` (`src/main.cpp`) · **Needs renderer:** yes
> **Source:** [`src/view/game.h`](../../../src/view/game.h), [`src/view/game.cpp`](../../../src/view/game.cpp)

## Purpose
The dynamical executable's application object. It owns the host `entt::registry`, the `Renderer` and the host `SystemSet`; emplaces the process-level context variables; loads the scene (optionally named by a capture script); builds the renderer, the host pre/post systems and the scene's [`Worlds`](../components/Worlds.md); runs preinit / init; then runs the frame loop (renderer prepare, frame clock, capture, pre and post ticks, probe, render, capture readback) until exit; and tears everything down in order.

## Lifecycle
- **Construction / params:** `Game(argc, argv)`:
  1. Creates the registry; emplaces `Settings(argc, argv)` (loads `./config.6.json`, writes it back, applies `key=value` overrides) and a default `tf::Executor`.
  2. `parseRunArgs(argc, argv)`: unknown keys other than the Settings keys (`window_width`, `window_height`, `fullscreen`, `fps_max`, `username`, `server_side`, `client_side`, `vr_mode`, `view_distance`) log a warning; parse errors log "Argument ignored"; emplaces `RunArgs`.
  3. If `RunArgs::deterministic()` (capture / probe / fixed dt): `Settings` window 960 x 540, not fullscreen, `vr_mode = 0` (not saved).
  4. Emplaces `FrameClock`.
- **preinit / init (in `start()`):**
  1. Scene name = `scene=` or, when `capture=` is set and `Capture::readScene` reads the script, the script's `scene` (default `base`); a different command-line scene is an error (return 1). Script `vars` are inserted before command-line vars. `loadScene(name or "base", vars)`; `SceneError` logs and returns 1. A scene without a `renderer` block returns 1 ("run it with dynamical_train").
  2. If the scene's renderer has `vr: false`, `Settings::vr_mode = 0`. Constructs `Renderer(reg, scene.renderer)` (Vulkan, OpenXR, render targets, passes; emplaces ModelManager, RenderSettings, Context*, VRRig, VRInput).
  3. Creates the `SystemSet`; adds each `host.pre` entry to Pre and each `host.post` entry to Post through `findSystem(name)->make(reg, params)` with its phase and registry name (constructors run here: InputSys emplaces `Input`).
  4. If the scene has worlds: emplaces `Worlds` and `create(scene, RunArgs::seed)`; `SceneError` returns 1.
  5. preinit: `renderer->preinit()`, `set->preinit()`, `worlds->preinit()`. init: `set->init()`, `worlds->init()`, `renderer->init()`.
  6. `reg.ctx().get<Input>()` (required), `RunArgs`, `FrameClock`. Creates [Capture](Capture.md) when `capture=` is set and `Probe` when `probe=` is set; a failed one logs its error and returns 1.
- **tick (each loop iteration):**
  1. `renderer->prepare()` (waits for the frame fence, acquires images, syncs VR input).
  2. If `Input::on[EXIT]`: `running = false`, clear the bit (the rest of this iteration still runs).
  3. `dt` = wall-clock seconds since the previous iteration, or `RunArgs::fixed_frame_dt` when deterministic. `FrameClock.dt = dt`, `time += dt`, `frame++` (first frame is 1).
  4. `capture->beginFrame(frame)`.
  5. `set->pre_tick(dt)`, `set->post_tick(dt)`.
  6. `probe->sample(reg, frame)`.
  7. `shoot = capture && capture->due(frame) && renderer->requestCapture()`; `renderer->render()`; if shooting, `renderer->takeCapture(pixels, w, h)` then `capture->save`, else `capture->error = "frame readback failed"`.
  8. Capture failure logs and returns 1; capture done with `exit_after_capture` stops; `ticks > 0` and `frame >= ticks` stops.
  9. Returns 0 when the loop ends.
- **finish (`~Game`):** `Worlds::finish()` if present, `set->finish()`, `renderer->finish()`; then destroys the SystemSet (systems own GPU buffers) before the Renderer; the registry (and its context variables, `Worlds` included) is destroyed last.

## Expected behavior
- Exit code 1 on any scene, capture or probe error, 0 otherwise.
- Escape / window close stop the loop one frame later (the EXIT bit is read before the systems tick).
- With `fixed_frame_dt` (default 1/60 under capture or probe) nothing in the loop reads the wall clock: `FrameClock`, WorldStepper ticks and UI DeltaTime are reproducible.
- `ticks=N` runs exactly N frames.
- A rendered scene must contain the `Input` host system (Game reads `Input` after init).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Settings`](../components/Settings.md) | host | write +create | constructor | emplaced; deterministic overrides |
| [`tf::Executor`](../components/tf-Executor.md) | host | write +create | constructor | emplaced |
| [`RunArgs`](../components/RunArgs.md) | host | write +create | constructor | emplaced |
| [`FrameClock`](../components/FrameClock.md) | host | write +create | constructor | emplaced |
| [`RunArgs`](../components/RunArgs.md) | host | read | start | scene, vars, capture, seed, probe, fixed dt, ticks, exit_after_capture |
| [`Settings`](../components/Settings.md) | host | write | start | `vr_mode = 0` when the scene disables VR |
| [`Worlds`](../components/Worlds.md) | host | write +create | start | emplaced and created when the scene has worlds |
| [`Input`](../components/Input.md) | host | readwrite | tick | reads and clears `on[EXIT]` |
| [`FrameClock`](../components/FrameClock.md) | host | write | tick | `dt`, `time`, `frame` |
| [`Worlds`](../components/Worlds.md) | host | readwrite | finish | `finish()` |

## Side effects
- Reads / writes `./config.6.json` (through Settings), reads the scene files, optional capture script, writes probe CSV and capture PNGs (through Probe / Capture).
- Creates the Vulkan / SDL / OpenXR renderer; drives GPU submission each frame.
- Logging of argument warnings and errors.
- Probe::sample (scene area) reads `Transform` / `RigidBody` of the host registry every frame when enabled.

## Ordering and dependencies
- Construction order matters: Renderer before systems (systems' constructors and inits use ModelManager, Context*, VRRig, VRInput); systems constructed before preinit; Worlds created after the host systems.
- Preinit order renderer -> host systems -> worlds; init order host systems -> worlds -> renderer.
- Frame order: prepare -> FrameClock -> Capture::beginFrame -> pre lane -> post lane -> Probe -> render -> capture readback.
- `main` must call `registerSimSystems()` / `registerViewSystems()` (and `registerPolicySystems()` with ONNX) and create `JoltRuntime` before constructing Game.

## Related
[Capture](Capture.md), [InputSys](InputSys.md), [WorldStepperSys](WorldStepperSys.md), [`docs/physical-agents/architecture.md`](../../physical-agents/architecture.md), [folder `src/view/`](../folders/src-view.md), [folder `src/`](../folders/src.md).
