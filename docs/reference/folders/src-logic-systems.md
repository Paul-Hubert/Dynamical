# `src/logic/systems/`

> [Source](../../../src/logic/systems/) · Parent: [`src/logic/`](src-logic.md)

## Summary
The system framework and a few simulation systems. `System` is the base class of every system (host or world); `SystemSet` holds the ordered lanes a registry ticks; `Phase` and the `Parked` world flag implement decision windows (`docs/physical-agents/simulation.md`). The systems here are the host chunk streaming pair (ChunkManager, ChunkGenerator) and the test-world setup system BoxDrop. Systems are created by name from scenes through `src/scene/system_registry.cpp` (`registerSystem`, `SystemScope` Host / World / Both); `Game` (src/view/game.cpp) fills the host set's Pre and Post lanes, `World` (src/scene/world.cpp) fills each world's Fixed lane.

### `System`
`System(entt::registry& reg)`; virtual `preinit()`, `init()`, `finish()` (no-ops), pure `tick(double dt)` and `name()`; `operator()(dt)` calls `tick`. `name()` is the class's own label; scenes use the registry name, which may differ.

### `SystemSet` (service)
- `enum class List { Pre, Post, Fixed }`. Each list is a vector of `Entry { unique_ptr<System>, Phase, std::string name }`, kept in insertion (scene) order.
- `add(list, system, phase = Every, name = "")`: appends; an empty name becomes `system->name()`. `pre_add<T>()` / `post_add<T>()` construct `T(reg)` into Pre / Post. Phases are stored for every list but only the Fixed lane uses them.
- `preinit()`, `init()`, `finish()`: call the hook on every system of Pre, then Post, then Fixed, in order. `Game` calls `preinit` on every host system before any `init`; `World::init` creates the template entities before `systems.init()`.
- `pre_tick(dt)` / `post_tick(dt)`: tick every system of the list with `dt` (host frame loop: `pre_tick`, then `post_tick`, around the renderer).
- `fixed_tick(h, act_tick, observe_tick)`: reads `Parked` (`find`, optional) from the set's registry; `skip = parked && parked->value`. Then for each Fixed entry in order: `Every` runs when `!skip`, `Act` when `!skip && act_tick`, `Observe` when `observe_tick` (even while parked). `World::tick` passes `act_tick = (tick % decision_period == 0)` and `observe_tick = (tick % decision_period == decision_period - 1)` (both true when the period is 1), then increments `WorldClock::tick`.
- `fixed_observe_only()`: ticks only the Observe systems with `h = 0`, ignoring `Parked` (WorldsEnvironment::reset: fresh observations without stepping).
- `names(list)`: the registry names of a list (tests).
- Thread safety: none internally; a set is ticked by one thread at a time. Different worlds' sets tick in parallel on the executor (`Worlds::step`).

### `Parked`
World context variable `{ bool value = false; }`, emplaced by the `World` constructor. Set by an agent whose episode ended mid-window (SphereTaskSys), cleared at the Observe phase (SphereObserveSys) or by `resetSphereEpisode`. While set, only the Observe systems of the world run.

## Files
| File | Contents |
|---|---|
| [`system.h`](../../../src/logic/systems/system.h) | `Phase { Every, Act, Observe }`, abstract `System` base class. |
| [`system_set.h`](../../../src/logic/systems/system_set.h) | `Parked` world flag, `SystemSet` (lists, add, lifecycle, `pre_tick`, `post_tick`, `fixed_tick`, `fixed_observe_only`, `names`). |
| [`system_set.cpp`](../../../src/logic/systems/system_set.cpp) | `SystemSet` implementation (phase gating, Parked check). |
| [`box_drop.h`](../../../src/logic/systems/box_drop.h) | `BoxDropSys` declaration and params (count, spacing, height, half_extent, capsule_every). |
| [`box_drop.cpp`](../../../src/logic/systems/box_drop.cpp) | `BoxDropSys` constructor (JSON params) and `init` (jittered grid of boxes / capsules from `WorldRng`). |
| [`chunk_systems.h`](../../../src/logic/systems/chunk_systems.h) | `ChunkManagerSys`, `ChunkGeneratorSys` (with its private `Job` record and job list). |
| [`chunk_systems.cpp`](../../../src/logic/systems/chunk_systems.cpp) | `loadCenters` (Camera, VR rig), chunk load / unload with hysteresis, generation jobs on taskflow (max 16, nearest first, synchronous when deterministic), `collect`. |

## Systems defined here
- [BoxDropSys](../systems/BoxDropSys.md)
- [ChunkManagerSys](../systems/ChunkManagerSys.md)
- [ChunkGeneratorSys](../systems/ChunkGeneratorSys.md)
- `SystemSet` (service, documented above; its only registry access is reading `Parked` in `fixed_tick`)

## Components defined here
- [Parked](../components/Parked.md)

## Subfolders
None.
