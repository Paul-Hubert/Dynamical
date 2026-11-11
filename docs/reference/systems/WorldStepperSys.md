# WorldStepperSys

> **Kind:** system · **Registry name:** `WorldStepper` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/world_stepper.h`](../../../src/view/world_stepper.h), [`src/view/world_stepper.cpp`](../../../src/view/world_stepper.cpp)

## Purpose
Advances the scene's [`Worlds`](../components/Worlds.md) in real time inside dynamical: a fixed-step accumulator fed with the frame dt runs whole world ticks (each world's fixed lane, physics included), at most 8 per frame, and publishes the interpolation factor in [`WorldStepperState`](../components/WorldStepperState.md). An ImGui panel "Worlds" offers pause and single step. Under capture / fixed-dt runs the frame dt is fixed, so the number of ticks per frame is reproducible.

## Lifecycle
- **Construction / params:** `WorldStepperSys(entt::registry&)`, no JSON params. Member `accumulator = 0`; `max_ticks = 8`.
- **preinit:** `reg.ctx().emplace<WorldStepperState>()`.
- **init:** None (default).
- **tick(dt):**
  1. Get the state; return (no panel either) if there is no `Worlds` context variable or it is empty.
  2. `state.h = worlds[0].reg.ctx().get<WorldClock>().dt`.
  3. Not paused: `accumulator += min(dt, 0.25)`; `ticks = min(int(accumulator / h), 8)`; `accumulator -= ticks * h`; if `accumulator > h` (could not keep up) reset it to 0. Paused with `step_once`: `ticks = 1`, clear `step_once`. Paused otherwise: `ticks = 0`.
  4. `worlds->step(ticks)` with no executor: every world runs `ticks` fixed ticks, sequentially, on the main thread.
  5. `state.ticks += ticks`; `state.alpha = paused ? 1 : accumulator / h`.
  6. ImGui window "Worlds" (first-use position (10, 380)): world count, total ticks, h; checkbox Pause (`state.paused`); button Step (sets `paused` and `step_once`).
- **finish:** None (default). Worlds are finished by [Game](Game.md).

## Expected behavior
- At 60 fps with h = 1/60, one tick per frame on average; `alpha` stays in [0, 1) while running.
- A frame longer than 0.25 s counts as 0.25 s; more than 8 ticks of backlog is dropped (no spiral of death), and any remainder above one tick is discarded.
- Paused: no ticks, `alpha = 1` (the latest pose is drawn). Step: exactly one tick on the next frame, then stays paused.
- All worlds are stepped with world 0's dt: worlds with a different `WorldClock::dt` run at the wrong real-time rate (assumption: one dt per scene).
- `alpha` and the Step button take effect for readers later in the same frame.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`WorldStepperState`](../components/WorldStepperState.md) | host | write +create | preinit | `emplace` |
| [`WorldStepperState`](../components/WorldStepperState.md) | host | readwrite | tick | `h`, `ticks`, `alpha`, `paused`, `step_once` |
| [`Worlds`](../components/Worlds.md) | host | readwrite | tick | `find`, `size()`, `step(ticks)` |
| [`WorldClock`](../components/WorldClock.md) | world | read | tick | world 0's `dt` |

## Side effects
- Runs the world fixed lanes (all world systems, Jolt steps in every world's `PhysicsWorld`, agent IO) synchronously.
- ImGui window "Worlds".

## Ordering and dependencies
- After [UISys](UISys.md) (ImGui).
- Before the systems that read this frame's world poses or steer the worlds: [RollerPlayerSys](RollerPlayerSys.md), [SphereViewSys](SphereViewSys.md), [WorldViewSys](WorldViewSys.md) (post lane). Commands written by RollerPlayer after the step act on the next frame's ticks.
- After host systems that feed the worlds (e.g. the ONNX `ModelWatcher`, which injects policies).
- `Worlds` is emplaced by Game::start before preinit.

## Related
[WorldViewSys](WorldViewSys.md), [SphereViewSys](SphereViewSys.md), [RollerPlayerSys](RollerPlayerSys.md), [Game](Game.md), [`docs/physical-agents/simulation.md`](../../physical-agents/simulation.md), [folder `src/view/`](../folders/src-view.md).
