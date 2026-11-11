# Worlds

> **Kind:** service · **Registry name:** — · **Scope:** Host · **Lane:** per-frame call from `WorldStepperSys` (dynamical) / `ScenarioRunner::step` · **Needs renderer:** no
> **Source:** [`src/scene/world.h`](../../../src/scene/world.h), [`src/scene/world.cpp`](../../../src/scene/world.cpp)

## Purpose
The list of a scene's [World](World.md)s in index order. In dynamical it is a host context variable (emplaced by `Game::start` when the scene has worlds) that `WorldStepperSys` steps and `WorldViewSys` / `SphereViewSys` / `RollerPlayerSys` / `ModelWatcherSys` read; `ScenarioRunner` owns one as a plain member. It does not access registry components itself: everything goes through `World`.

## Lifecycle
- **Construction / params:** default constructed, not copyable. `create(const SceneDef& scene, uint64_t base_seed)`: for each `WorldDef` of `scene.worlds` in order and each of its `count` copies, appends `World(index, worldSeed(base_seed, index), def)` with `index` starting at the current `list.size()` and increasing by one (indices are global across world entries). `worldSeed(b, i) = splitmix64(splitmix64(b) ^ (0x632be59bd9b4e019 * (i + 1)))`. May throw `SceneError` from a world's physics or clock block.
- **preinit:** `preinit()` of every world, in index order.
- **init:** `init()` of every world, in index order.
- **tick(dt):** `step(int64_t ticks, tf::Executor* executor = nullptr)`: each world runs `ticks` consecutive `World::tick()` calls. Without an executor or with fewer than 2 worlds, sequentially in index order; otherwise one taskflow `for_each` task per world on `executor`, and `step` blocks until all are done (`run(flow).wait()`).
- **finish:** `finish()` of every world (idempotent per world). The worlds are destroyed with `list`.

## Expected behavior
- `size()` equals the sum of `count` over the scene's world entries; `(*this)[i].index == i`.
- `count: 0` yields no world for that entry.
- Results are independent of the executor and thread count (`tests/unit/test_boxes.cpp` S2.2: 1 and 8 threads give equal hashes).
- Calling `create` twice appends; `ScenarioRunner::restart` clears `list` first.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| None directly | — | — | — | see [World](World.md); the `Worlds` object itself is emplaced in the host registry by `Game::start` (view area) |

## Side effects
- Allocates the worlds (each with a Jolt physics system).
- `step` with an executor spawns one taskflow task per world and waits for them.

## Ordering and dependencies
- In dynamical, `create` runs after the host systems are added and before `renderer->preinit()`; `preinit` after the host `preinit`, `init` after the host `init`; `finish` in `~Game` before the host systems finish.
- Worlds are stepped by `WorldStepperSys` in the host pre or post lane; anything reading world registries from the host (views, policy) must run on the main thread outside `step`.

## Related
[World](World.md), [ScenarioRunner](ScenarioRunner.md), [folder `src/scene/`](../folders/src-scene.md), [Worlds component](../components/Worlds.md).
