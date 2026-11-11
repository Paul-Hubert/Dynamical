# SceneLoader

> **Kind:** service · **Registry name:** — · **Scope:** n/a · **Lane:** one-shot call (`loadScene`) from `Game::start`, `ScenarioRunner`, tests · **Needs renderer:** no
> **Source:** [`src/scene/scene_def.h`](../../../src/scene/scene_def.h), [`src/scene/scene_loader.cpp`](../../../src/scene/scene_loader.cpp)

## Purpose
Turns a scene file (`resources/scenes/<name>.scene.json` or a path ending in `.json`) into a `SceneDef`: includes collected, variables declared and overridden, `${var}` and `"<file>.json#<key>"` substituted, prefabs merged into entities, world templates resolved into `WorldDef`s, host system lists deduplicated, and a `resolved` JSON dump that is loadable as a scene. The implementation class is `Loader` in an anonymous namespace; the public entry points are `loadScene`, `loadPrefab` and `writeResolved`. Every error is a `SceneError` carrying the file (as written in the scene tree, or `<command line>`) and a JSON pointer. To report component errors at load time it emplaces every entity's components into a throwaway registry; that is its only registry access.

## Lifecycle
- **Construction / params:** `loadScene(name, vars, SceneLoadOptions{ resources = "resources", allow_renderer = true })`. `vars` are `var.<name>=<value>` pairs in command-line order.
- **preinit:** None.
- **init:** None.
- **tick(dt):** the single call `Loader::load`, in order:
  1. Resolve the file: a name ending in `.json` is used as a path; otherwise `<resources>/scenes/<name>.scene.json`. Missing: "scene not found".
  2. `collect`: parse the file (JSON with comments allowed), recurse into `include` (array of names relative to `scenes/`), detecting cycles on the include stack; each file is collected once (includes first, scene last).
  3. `checkTopLevel` per file: allowed keys `format_version, name, include, variables, renderer, host, templates, worlds, resolved_from`. With `allow_renderer == false` any `renderer` block fails first. `resolved_from` (`scene`, `includes`) adds its includes to the include set and is kept.
  4. `readVariables` per file: each `variables.<name>` is `{ type: int|float|bool|string, default, description? }`; the default must match the type; redeclaring with another type fails; a later file's default overrides an earlier one. Floats stored as double.
  5. `applyCommandLine`: each `var.<name>` must name a declared variable; parsed with `from_chars` (int64 / double, whole string), bool `true|false|1|0`, string as is. Last wins.
  6. Raw templates (before substitution) are remembered per name for per-world re-resolution.
  7. `substitute` per file (skipping top-level `variables`, `include`, `resolved_from`): a string that is exactly `${v}` becomes the typed value; strings containing `${v}` get the text of each value spliced in (unterminated `${` fails); then a string matching `^(.+\.json)#(.+)$` is replaced by the value at key (top-level name, or JSON pointer when it starts with `/`) of `<resources>/<file>` (files cached).
  8. `validate` per file: `format_version` must be 1 if present; `name` a string; `renderer` `{ passes: [unique names from terrain, grass, model, ui], vr: bool }`; `host` `{ pre, post }` system lists at host scope; `templates.<name>` `{ physics: object, clock: object, fixed: system list at world scope, entities: array }`; `worlds[i]` `{ template: string (required), count: int >= 0, systems: { replace: object, remove: array }, variables: object, variable_files: [existing files under scenes/] }`.
     - A system is `"Name"`, `"Name@act"`, `"Name@observe"` or `{ name, params: object }`; phases are refused at host scope; the name must be registered; a World-scope system at host scope, a Host-scope system in a world, and a `needs_renderer` system without `allow_renderer` are refused.
     - An entity is an object of components plus optional `prefab` (must exist in `prefabs/`); every key must be a registered component with an object value; prefab fields are deep-merged under the entity's own; then `validateComponents`.
  9. `merge`: `name` from the last file that sets it; `renderer` from the last file that has one, which then requires `common/desktop.json` among the includes; host `pre`/`post` concatenated over files and deduplicated by name keeping the last occurrence (a later file moves a system); templates merged by name (last wins); worlds concatenated. For a world entry with `variables` / `variable_files`, the raw template is re-substituted with the files' variable defaults then the entry's typed values, stored as template `<template>@<entry index>`, and the entry's variable keys are dropped. Each world entry becomes a `WorldDef` (`template_name` without the `@` suffix, `count` default 1, `physics`, `clock`, `fixed`, entities). `systems.replace` replaces a fixed entry matched by name and phase (`"from": to`); `systems.remove` erases entries; an unmatched name fails.
  10. Build `resolved`: `format_version: 1`, `name`, final variables (`type`, `default` = effective value), `renderer`, deduplicated `host`, all templates (including `@n` ones), worlds, and `resolved_from` (the original one if the scene was itself a resolved dump, otherwise `{ scene, includes }`).
- **finish:** None. `writeResolved(scene, file)` creates the parent directories and writes `resolved.dump(2)`.

## Expected behavior
- Malformed scenes fail with the file and JSON path of the culprit (`tests/unit/scene_loader.cpp` T1.3; fixtures in `tests/data/scene/resources/scenes/bad/`).
- Command-line variables override defaults, last wins, typed (T1.4); a type mismatch fails with file `<command line>` and path `var.<name>`.
- Loading `resolved` again yields the same scene (T1.4 round trip); world entries with their own variables survive the dump (T1.5).
- Prefabs: `extends` chains resolve parent first, cycles fail at `/extends`; each prefab's own components are validated.
- `ModelRenderable` (registered only by `registerViewSystems`) can never appear in a scene entity or prefab: validation runs in a registry without `ModelManager`, so its emplace always throws "ModelRenderable needs the renderer". Scenes use `ModelRef` instead.
- dynamical_train (`allow_renderer = false`) refuses any `renderer` block and any `needs_renderer` system.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [Transform](../components/Transform.md) | scratch | write +create | load | `validateComponents`: emplace into a local registry, discarded |
| [RigidBody](../components/RigidBody.md) | scratch | write +create | load | same |
| [Ground](../components/Ground.md) | scratch | write +create | load | same |
| [HeightField](../components/HeightField.md) | scratch | write +create | load | same |
| [ModelRef](../components/ModelRef.md) | scratch | write +create | load | same |
| [SphereAgent](../components/SphereAgent.md) | scratch | write +create | load | same |
| [ModelManager](../components/ModelManager.md) | scratch | read | load | `ModelRenderable`'s emplace calls `ctx().find<ModelManager>()`, always null here |

## Side effects
- Reads scene files, included files, `variable_files`, prefabs and `"<file>#<key>"` reference files from disk.
- Creates one scratch `entt::registry` per validated entity or prefab (destroyed on return).
- `writeResolved` writes a file (dynamical_train writes `<log dir>/scene.resolved.json`).
- No logging; no RNG.

## Ordering and dependencies
- Needs the system and component registries filled first (`registerSimSystems()`, and `registerViewSystems()` / `registerPolicySystems()` for scenes using them; tests use stubs from `tests/support/test_systems.cpp`).
- Validation of entities happens twice: once per file in `validate` and again in `merge` (the merged definitions).
- Not thread safe with concurrent `registerComponent` / `registerSystem` calls (plain `std::map`s); concurrent loads are otherwise independent.

## Related
[World](World.md), [ScenarioRunner](ScenarioRunner.md), [folder `src/scene/`](../folders/src-scene.md) (component JSON keys), `docs/physical-agents/scenes.md`.
