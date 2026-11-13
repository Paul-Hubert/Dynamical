# src/scene

Scenes as data: the JSON scene loader (includes, variables, prefabs, world templates), the registries of systems
and components by name, the command line (`RunArgs`), and the worlds that run a scene's simulations (`World`,
`Worlds`, `ScenarioRunner`). Part of `dynamical_sim`: no renderer.

| File | What |
|---|---|
| [`scene_def.h`](../../../src/scene/scene_def.h) | `SceneDef`, `WorldDef`, `EntityDef`, `SystemEntry`, `RendererDef`, `SceneLoadOptions`, `SceneError` (file + JSON pointer), `scene_passes`; `loadScene`, `loadPrefab`, `writeResolved` |
| [`scene_loader.cpp`](../../../src/scene/scene_loader.cpp) | the loader: `include` chains, typed `variables` with command line overrides (`var.<name>=`, last wins) and `${name}` substitution, `"<file>#<key>"` references, prefabs with `extends`, world templates with per-world `variables`, `variable_files`, `systems.replace` / `remove`, `@act` / `@observe` phases, scope and renderer checks, the resolved dump |
| [`system_registry.h`](../../../src/scene/system_registry.h) / [`system_registry.cpp`](../../../src/scene/system_registry.cpp) | `SystemScope`, `SystemInfo` (factory, scope, needs renderer), `registerSystem`, `findSystem`, `systemInfo<T>`; `registerSimSystems()` registers the library's systems and the JSON forms of `Transform`, `RigidBody`, `Ground`, `HeightField`, `PointLight`, `ModelRef`, then every area's `register*()` |
| [`component_registry.h`](../../../src/scene/component_registry.h) / [`component_registry.cpp`](../../../src/scene/component_registry.cpp) | `ComponentInfo` (emplace from JSON, save to JSON), `registerComponent`, `findComponent`, `ComponentError`, `JsonFields` (typed optional fields, enums by name, unknown keys refused), `toJson` |
| [`run_args.h`](../../../src/scene/run_args.h) / [`run_args.cpp`](../../../src/scene/run_args.cpp) | `RunArgs` (scene, process, capture, probe, lighting keys) and `parseRunArgs` (unknown keys and bad values reported; capture or probe default to a fixed 1/60 clock and a hidden window) |
| [`world.h`](../../../src/scene/world.h) / [`world.cpp`](../../../src/scene/world.cpp) | `WorldClock`, `WorldRng`, `WorldInfo`, `worldSeed`, `physicsConfig`, `worldClock`, `World` (registry, Jolt world, fixed lane, template entities, `hash`), `Worlds` (create, preinit, init, step in parallel, finish) |
| [`scenario_runner.h`](../../../src/scene/scenario_runner.h) / [`scenario_runner.cpp`](../../../src/scene/scenario_runner.cpp) | `ScenarioRunner`: load a scene, create and init its worlds, step them headless, `restart(seed)` |
| [`probe.h`](../../../src/scene/probe.h) / [`probe.cpp`](../../../src/scene/probe.cpp) | `Probe`: CSV of the poses of `RigidBody` or `Transform` entities every `probe_every` frames |

## Systems registered here

`registerSimSystems()` in `system_registry.cpp` registers:
[ChunkManager](../systems/ChunkManager.md), [ChunkGenerator](../systems/ChunkGenerator.md),
[RigidBody](../systems/RigidBody.md), [TerrainColliders](../systems/TerrainColliders.md),
[Physics](../systems/Physics.md), [PhysicsStep](../systems/PhysicsStep.md),
[WorldTerrain](../systems/WorldTerrain.md), [BoxDrop](../systems/BoxDrop.md); then calls `registerPlayer`,
`registerSky`, `registerSphereRoller`, `registerPursuit`, `registerHumanBot`, `registerIK`, `registerGroundField`,
`registerGait`, `registerWalkRoutes`, `registerVegetation`, `registerForest`, `registerAnimals`,
`registerAnimalAgents`, `registerAnimalSteer`, `registerSandbox` (their systems are on the pages of those areas).
The objects that run them: [service-Game](../systems/service-Game.md).

## Components and context defined here

- [RunArgs](../components/RunArgs.md) (host context, `Game`)
- [WorldClock](../components/WorldClock.md), [WorldRng](../components/WorldRng.md), [WorldInfo](../components/WorldInfo.md) (world context, `World`)
- [Worlds](../components/Worlds.md) (host context in `dynamical`, `Game::start`)

JSON names registered here: `Transform`, `RigidBody`, `Ground`, `HeightField`, `PointLight`, `ModelRef`.

## Main functions and classes

- `loadScene(name, vars, options)`: `resources/scenes/<name>.scene.json` or a path; throws `SceneError` with the
  file and the JSON pointer.
- `findSystem(name)->make(reg, params)`: how `Game` and `World` build systems.
- `JsonFields`: how every registered component reads its JSON (unknown keys are errors).
- `worldSeed(base, index)`, `WorldRng` (its own distributions: identical on every standard library).
- `World::hash()`: the determinism check of the physics tests.

## Dependencies

[`src/logic/systems`](src-logic-systems.md) (`System`, `SystemSet`), [`src/physics`](src-physics.md)
(`PhysicsWorld`, `PhysicsConfig`), [`src/logic/components`](src-logic-components.md), every `logic/*` area for its
`register*()`, [`src/util`](src-util.md) (`paths`, profiler), nlohmann-json, taskflow, Jolt.

## Design docs

[docs/physical-agents/scenes.md](../../physical-agents/scenes.md) (scene format),
[docs/physical-agents/simulation.md](../../physical-agents/simulation.md) (worlds, clock, decision windows, seeds),
[docs/physical-agents/testing.md](../../physical-agents/testing.md) (probes, determinism).
