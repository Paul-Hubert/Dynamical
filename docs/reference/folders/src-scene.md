# `src/scene/`

> [Source](../../../src/scene/) · Parent: [`src/`](src.md)

## Summary
Scenes and worlds, part of the `dynamical_sim` library (no renderer code). A scene file (`resources/scenes/*.scene.json`) names the render passes, the host systems and a list of world templates; `loadScene` resolves it into a `SceneDef`. Each world entry becomes `count` independent `World`s with their own `entt::registry`, Jolt `PhysicsWorld`, `WorldClock`, `WorldRng`, `WorldInfo` and fixed-lane `SystemSet`, collected in `Worlds`. Systems and components are looked up by name in two global registries filled by `registerSimSystems()` (here), `registerSphereRoller()` (`src/logic/agents`), `registerPolicySystems()` (`src/onnx`) and `registerViewSystems()` (`src/view`). `ScenarioRunner` steps worlds headless (tests, `dynamical_train`); `RunArgs` parses the shared `key=value` process arguments; `Probe` writes behaviour CSVs from the host registry in dynamical.

## Files
| File | Contents |
|---|---|
| `scene_def.h` | `SceneError` (file + JSON pointer), `SystemEntry`, `EntityDef`, `WorldDef`, `RendererDef`, `SceneDef`, `SceneLoadOptions`, `scene_passes` (`terrain, grass, model, ui`), declarations of `loadScene`, `loadPrefab`, `writeResolved` |
| `scene_loader.cpp` | `Loader` (includes, variables, `${}` and `"<file>.json#<key>"` substitution, validation, merge, resolved dump), `loadPrefabChain` (`extends`), `validateComponents` (scratch-registry emplace) |
| `component_registry.h/.cpp` | `ComponentInfo { emplace, save }`, `registerComponent`, `findComponent`, `ComponentError`, `JsonFields` (typed optional reads, `readEnum`, `finish` rejects unknown keys), `toJson` for vec3/vec4/quat |
| `system_registry.h/.cpp` | `SystemScope { Host, World, Both }`, `SystemFactory`, `SystemInfo { make, scope, needs_renderer }`, `registerSystem`, `findSystem`, `systemInfo<T>`, `registerSimSystems` (library systems plus the `Transform`, `RigidBody`, `Ground`, `HeightField`, `ModelRef` JSON forms) |
| `world.h/.cpp` | `WorldClock`, `WorldRng`, `WorldInfo`, `worldSeed`, `physicsConfig`, `worldClock`, `World`, `Worlds` |
| `scenario_runner.h/.cpp` | `ScenarioRunner`: load, create, step, restart worlds headless |
| `run_args.h/.cpp` | `RunArgs`, `RunArgsResult`, `parseRunArgs` (scene, `var.*`, process, capture and probe keys) |
| `probe.h/.cpp` | `Probe`: CSV of entity poses every `probe_every` frames |

## Registered systems (`registerSimSystems`)
| Registry name | Class | Scope | Needs renderer | Params |
|---|---|---|---|---|
| `ChunkManager` | `ChunkManagerSys` | Host | no | none |
| `ChunkGenerator` | `ChunkGeneratorSys` | Host | no | none |
| `RigidBody` | `RigidBodySys` | Both | no | none |
| `TerrainColliders` | `TerrainCollidersSys` | Host | no | none |
| `Physics` | `PhysicsSys` | Host | no | none |
| `PhysicsStep` | `PhysicsStepSys` | World | no | none |
| `WorldTerrain` | `WorldTerrainSys` | World | no | JSON `params` |
| `BoxDrop` | `BoxDropSys` | World | no | JSON `params` |

`registerSimSystems` then registers the components below and calls `registerSphereRoller()` (`SphereControl`, `SphereTask`, `SphereObserve`, `TrainerActions`, `SphereHeuristic`, all World). Registering a name twice replaces it. Parameter keys of the parameterised systems are documented in their system docs.

## Scene JSON components
Every component in an entity or prefab is an object of optional fields read with `JsonFields`: missing keys keep the C++ default, unknown keys and wrong types fail (`ComponentError` with the field path, reported as `SceneError`). vec*/quat fields are arrays of numbers; quaternions are `[x, y, z, w]`; enums are strings. All emplace with `emplace_or_replace`.

| Component | Registered in | Keys (type, default) | Validation |
|---|---|---|---|
| `Transform` | `system_registry.cpp` | `position` vec3 `[0,0,0]`; `rotation` quat `[0,0,0,1]`; `scale` vec3 `[1,1,1]` | none |
| `RigidBody` | `system_registry.cpp` | `motion` `static`/`dynamic`/`kinematic` (`dynamic`); `shape` `box`/`sphere`/`capsule` (`box`); `half_extent` vec3 `[0.5,0.5,0.5]`; `offset` vec3 `[0,0,0]`; `friction` float 0.6; `restitution` float 0.1; `angular_damping` float 0.05; `mass` float 0 (0: density 1000 kg/m³); `initial_velocity` vec3 0; `initial_angular_velocity` vec3 0 | none |
| `Ground` | `system_registry.cpp` | `size` vec3 `[150,10,150]` (full extents); `top_y` float 0; `friction` float 1; `restitution` float 0 | every `size` component > 0 |
| `HeightField` | `system_registry.cpp` | `samples` int 256; `spacing` float 0.5; `waves` int 6; `wavelength` vec2 `[6,40]`; `wave_slope` float 0.07; `friction` float 1; `restitution` float 0; `seed` int 0 (0: one draw of the world RNG) | `samples` power of 2 >= 8; `spacing` > 0; `waves` >= 0; `0 < wavelength.x <= wavelength.y`; `seed` >= 0 |
| `ModelRef` | `system_registry.cpp` | `path` string (required, under `resources/`); `color` vec4 `[1,1,1,1]` | `path` non-empty |
| `SphereAgent` | `logic/agents/sphere_roller.cpp` | `behavior` string `SphereRoller`; `max_torque` 20; `speed_range` vec2 `[0.5,3]`; `stop_chance` 0.1; `turn_rate` 0.7; `acceleration` 0.75; `goal_interval` vec2 `[2,6]`; `max_goal_turn` 2.1; `home_radius` 30; `bounds` 55; `fall_depth` 100; `spawn_radius` 10; `spawn_spacing` 0; `stagger` bool true; `reward_scale` 1; `probe_distance` 1; `max_decisions` int 600; `steered` bool false (floats unless noted) | `behavior` non-empty; `max_decisions` >= 1; `0 <= speed_range.x <= speed_range.y`; `0 < goal_interval.x <= goal_interval.y`; `reward_scale` > 0 |
| `ModelRenderable` | `view/register_view.cpp` | `path` string; `tint` vec4 `[1,1,1,1]` | needs `ModelManager` in the registry, so it is always rejected by the scene loader (scratch registry) and in world registries |

The `save` functions write the same keys back (`ModelRenderable` saves only `tint`).

## Scene file keys (loader)
- Top level: `format_version` (1), `name`, `include` (files under `scenes/`), `variables` (`{ type: int|float|bool|string, default, description }`), `renderer` (`{ passes, vr }`, requires `common/desktop.json` among the includes), `host` (`{ pre, post }` system lists), `templates` (`{ physics, clock, fixed, entities }`), `worlds` (`[{ template, count, systems: { replace, remove }, variables, variable_files }]`), `resolved_from`.
- Systems: `"Name"`, `"Name@act"`, `"Name@observe"` (world only) or `{ "name", "params" }`.
- Entities: `{ "prefab": "<name>", "<Component>": { fields } ... }`; prefabs (`resources/prefabs/<name>.json`) are component maps with optional `extends`.
- Template `physics` keys: see [World](../systems/World.md); `clock`: `physics_hz` (60), `control_hz` (= physics_hz).

## Systems defined here
[World](../systems/World.md), [Worlds](../systems/Worlds.md), [ScenarioRunner](../systems/ScenarioRunner.md), [SceneLoader](../systems/SceneLoader.md), [Probe](../systems/Probe.md) (all `service`; no `dy::System` subclass is defined here).

## Components defined here
[WorldClock](../components/WorldClock.md), [WorldRng](../components/WorldRng.md), [WorldInfo](../components/WorldInfo.md) (world context), [Worlds](../components/Worlds.md), [RunArgs](../components/RunArgs.md) (host context in dynamical).

## Subfolders
None.
