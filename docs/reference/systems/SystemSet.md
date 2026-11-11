# SystemSet

> **Kind:** service · **Registry name:** — · **Scope:** n/a (one per registry: the host's in `Game`, one per `World`) · **Lane:** owns the Pre / Post / Fixed lanes · **Needs renderer:** no
> **Source:** [`src/logic/systems/system_set.h`](../../../src/logic/systems/system_set.h), [`src/logic/systems/system_set.cpp`](../../../src/logic/systems/system_set.cpp)

## Purpose
Holds the ordered systems of one registry in three lanes and runs their lifecycle hooks. The host set (`Game`) uses the Pre and Post lanes once per frame; each `World` uses the Fixed lane once per fixed tick, gated by the decision window `Phase` of each entry and the world's `Parked` flag.

## Lifecycle
- **Construction / params:** `SystemSet(entt::registry& reg)`. Systems are appended with `add(list, unique_ptr<System>, phase = Phase::Every, name = "")` (an empty name becomes `System::name()`), or `pre_add<T>()` / `post_add<T>()`. Entries keep insertion (scene file) order. Phases are stored for every lane but only read in the Fixed lane.
- **preinit:** calls `preinit()` on every system of Pre, then Post, then Fixed.
- **init:** calls `init()` in the same order. `World::init` creates the template entities before calling it.
- **tick:**
  - `pre_tick(dt)` / `post_tick(dt)`: tick every system of the lane with `dt`.
  - `fixed_tick(h, act_tick, observe_tick)`: `find<Parked>()` on the registry, `skip = parked && parked->value`; then each Fixed entry in order runs when its phase allows: `Every` when `!skip`, `Act` when `!skip && act_tick`, `Observe` when `observe_tick` (even while parked). Ticked with `h`.
  - `fixed_observe_only()`: ticks only the Observe systems with `h = 0`, ignoring `Parked` (reset path: fresh observations without stepping).
- **finish:** calls `finish()` on every system, Pre, then Post, then Fixed.

## Expected behavior
- Systems run in exactly the order they were added, within each lane.
- With `decision_period == 1`, `World::tick` passes `act_tick = observe_tick = true`, so every Fixed system runs every tick (unless parked).
- While `Parked::value` is true only Observe systems run; Every and Act systems are skipped.
- A registry without a `Parked` context variable is never parked (the host registry).
- `names(list)` returns the registry names of a lane in order (used by tests).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [Parked](../components/Parked.md) | world | read | fixed_tick | `ctx().find<Parked>()`; optional |

## Side effects
None of its own; everything happens in the systems it ticks. `OPTICK_EVENT` profiling scopes in `pre_tick`, `post_tick` and `fixed_tick`.

## Ordering and dependencies
The caller decides the lane order: `Game` runs `pre_tick`, then the renderer prepare/update, then `post_tick`; `World::tick` runs `fixed_tick` then increments `WorldClock::tick`. Not thread-safe; one set is ticked by one thread at a time, but different worlds' sets may tick in parallel (`Worlds::step` on the executor).

## Related
[World](World.md), [Worlds](Worlds.md), [Game](Game.md), folder doc [`src/logic/systems/`](../folders/src-logic-systems.md), [simulation design](../../physical-agents/simulation.md).
