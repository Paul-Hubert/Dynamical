# World

> **Kind:** service · **Registry name:** — · **Scope:** World · **Lane:** per-frame call from `Worlds::step` (`WorldStepperSys` in dynamical, `ScenarioRunner::step` headless) · **Needs renderer:** no
> **Source:** [`src/scene/world.h`](../../../src/scene/world.h), [`src/scene/world.cpp`](../../../src/scene/world.cpp)

## Purpose
One independent simulation built from a `WorldDef` (a resolved world template). A `World` owns its own `entt::registry`, its Jolt `PhysicsWorld`, its `WorldClock`, `WorldRng`, `WorldInfo` and `Parked` context variables, and a `SystemSet` whose fixed lane holds the template's systems. `tick()` advances one fixed tick and selects the Act / Observe phases of the decision window. Nothing in a world touches the host registry, so worlds can step concurrently.

## Lifecycle
- **Construction / params:** `World(uint32_t index, uint64_t seed, const WorldDef& def)`. In order: copies `def.entities` and `def.template_name`; emplaces in `reg.ctx()`:
  - `PhysicsWorld(physicsConfig(def))`. `physicsConfig` reads the template's `physics` object with `JsonFields` (unknown keys rejected): `threads` int (default 0), `max_bodies` int (65536), `max_body_pairs` int (65536), `max_contacts` int (16384), `temp_bytes` int (16 MiB), `gravity` vec3 (`[0, -9.81, 0]`), `substeps` int (1), `allow_sleeping` bool (true), `friction_combine` / `restitution_combine` enum `geometric | max | min` (geometric), `velocity_steps`, `position_steps` int, `speculative_distance`, `penetration_slop` float (each applied only when >= 0, otherwise Jolt's default). Validation: `threads >= 0`, `substeps >= 1`, `max_bodies >= 1` (the other counts are not range-checked and are cast to `uint32_t`). Errors become `SceneError(def.file, def.path + "/physics" + field)`.
  - `WorldClock(worldClock(def))`. Reads `clock`: `physics_hz` int (60), `control_hz` int (0 = `physics_hz`). Requires `physics_hz > 0`, `control_hz > 0`, `physics_hz % control_hz == 0`; `dt = 1 / physics_hz`, `decision_period = physics_hz / control_hz`. Errors at `<path>/clock`.
  - `WorldRng(seed)`, `WorldInfo{ index, seed, def.template_name }`, `Parked{}`.
  - Then, for every `SystemEntry` of `def.fixed` in order, `findSystem(name)->make(reg, params)` added to the Fixed list with its phase and registry name (system constructors therefore already see the context variables above).
- **preinit:** `systems.preinit()`.
- **init:** for each stored `EntityDef`, `reg.create()` then, for each `(name, fields)` of its component object (nlohmann object order: alphabetical by component name), `findComponent(name)->emplace(reg, entity, fields)`; a `ComponentError` is rethrown as `SceneError(e.file, e.path + "/" + name + error.path)`. Then `systems.init()`.
- **tick():** `k = clock.tick % clock.decision_period`; `systems.fixed_tick(clock.dt, k == 0, k == decision_period - 1)`; `clock.tick++`. With `decision_period == 1` every tick is both Act and Observe. While `Parked.value` is true, `SystemSet::fixed_tick` runs only Observe systems.
- **finish:** `systems.finish()` once (guarded by `finished`); also called from `~World`.

## Expected behavior
- Two worlds with the same seed and template produce identical `hash()` values after the same number of ticks; different seeds generally differ (`tests/unit/test_boxes.cpp` S2.3).
- `WorldClock.tick` equals the number of `tick()` calls; Act systems run on ticks where `tick % period == 0`, Observe systems where `tick % period == period - 1`.
- A template entity whose fields fail to build throws `SceneError` from `init()` naming the template file and JSON pointer (normally already caught by the loader's validation).
- `hash()` returns the FNV-1a offset basis when the world has no `PhysicsWorld`; otherwise FNV-1a over `(px, py, pz, qx, qy, qz, qw)` as floats of every created `RigidBody` body, entities sorted by id; uncreated bodies are skipped.
- `finish()` is idempotent; the destructor calls it.
- Copy construction and assignment are deleted.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | world | write +emplace_ctx | construct | from the template's `physics` block |
| [WorldClock](../components/WorldClock.md) | world | write +emplace_ctx | construct | from the template's `clock` block |
| [WorldRng](../components/WorldRng.md) | world | write +emplace_ctx | construct | seeded `worldSeed(base_seed, index)` |
| [WorldInfo](../components/WorldInfo.md) | world | write +emplace_ctx | construct | index, seed, template name |
| [Parked](../components/Parked.md) | world | write +emplace_ctx | construct | default `false` |
| [Transform](../components/Transform.md) | world | write +create | init | template entities, via the component registry |
| [RigidBody](../components/RigidBody.md) | world | write +create | init | template entities, via the component registry |
| [Ground](../components/Ground.md) | world | write +create | init | template entities, via the component registry |
| [HeightField](../components/HeightField.md) | world | write +create | init | template entities, via the component registry |
| [ModelRef](../components/ModelRef.md) | world | write +create | init | template entities, via the component registry |
| [SphereAgent](../components/SphereAgent.md) | world | write +create | init | template entities, via the component registry (registered by `registerSphereRoller`) |
| [WorldClock](../components/WorldClock.md) | world | readwrite | tick | phase selection, `tick++` |
| [RigidBody](../components/RigidBody.md) | world | read | hash | body ids |
| [PhysicsWorld](../components/PhysicsWorld.md) | world | read | hash | body positions and rotations |

## Side effects
- Construction creates a Jolt `PhysicsSystem` (with its job system and temp allocator, per `threads`) through `PhysicsWorld`; requires a live `JoltRuntime`.
- Constructs the template's fixed systems (they may emplace further context variables, e.g. `AgentIO`, `FixedStepClock`).
- `init()` creates entities in the world registry.
- No logging, files or RNG consumption of its own.

## Ordering and dependencies
- Must be constructed after `registerSimSystems()` (and `registerPolicySystems()` / `registerViewSystems()` when the template uses them): `findSystem` / `findComponent` results are dereferenced without a null check, relying on the loader's validation.
- `preinit` before `init` before `tick`; `Worlds` and `ScenarioRunner` call them in that order for all worlds.
- Thread safety: a world touches only its own registry and physics system, so distinct worlds may tick concurrently (`Worlds::step` with an executor). Ticking one world from two threads is not safe.

## Related
[Worlds](Worlds.md), [ScenarioRunner](ScenarioRunner.md), [SceneLoader](SceneLoader.md), [WorldsEnvironment](WorldsEnvironment.md), [folder `src/scene/`](../folders/src-scene.md), `docs/physical-agents/simulation.md`.
