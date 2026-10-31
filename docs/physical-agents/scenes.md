# Scenes

A scene is a JSON file in `resources/scenes/` that defines one way of running the engine: its
variables, whether it renders, the host systems, and N worlds with their systems and content. Both
executables load scenes with the same loader: `dynamical scene=<name>` and
`dynamical_train scene=<name>`.

## File format

```jsonc
{
  "format_version": 1,
  "name": "walker_view",
  "include": ["common/humanbot_world.json", "common/desktop.json"],   // merged in order, this file last

  "variables": {
    "num_worlds": { "type": "int",    "default": 1 },
    "policy":     { "type": "string", "default": "latest:results/${run}/HeidelBotBehavior" },
    "run":        { "type": "string", "default": "" },
    "params":     { "type": "string", "default": "agents/params/walker.params.json" }
  },

  "renderer": { "passes": ["model", "ui"], "vr": false },      // omitted = no renderer (required in dynamical_train)

  "host": {
    "pre":  ["Input", "UI", "PlayerControl", "DebugMenu", "AgentPanel",
             { "name": "WorldStepper", "params": { "realtime": true } }],
    "post": [{ "name": "WorldView", "params": { "worlds": "all", "spacing": 6.0 } }]
  },

  "worlds": [
    {
      "template": "humanbot_world",                            // from an include
      "count": "${num_worlds}",
      "systems": { "replace": { "TrainerActions": "Policy" } },
      "set": { "AgentSpawn.policy": "${policy}" }
    }
  ]
}
```

### World templates

```jsonc
// resources/scenes/common/humanbot_world.json
{
  "templates": {
    "humanbot_world": {
      "physics": { "threads": 1, "max_bodies": 128, "gravity": [0, -9.81, 0], "solver": "${params}#solver" },
      "clock":   { "dt": "${params}#dt", "decision_period": "${params}#decision_period" },
      "fixed": [
        "TrainerActions@act", "DriveTargets", "Disturbances", "PhysicsStep",
        "BodySync", "Energy", "Contacts", "Stats", "Termination", "Observation@observe", "Episode@observe"
      ],
      "entities": [
        { "prefab": "ground_flat" },
        { "prefab": "humanbot", "AgentSpawn": { "body": "agents/bodies/heidelbot.body.json", "params": "${params}" } }
      ]
    }
  }
}
```

### Rules

- **Variables** have a type and a default. They are overridden, last wins, by:
  1. the command line, `var.<name>=<value>`;
  2. `dynamical_train`'s Unity-style flags, where a variable declares `"flag": "-envParams="`;
  3. nothing else.

  ML-Agents environment parameters are **not** scene variables: they belong to the agent parameters
  and follow Unity's precedence (UA §9.3) ([mlagents-bridge.md](mlagents-bridge.md#environment-parameters)).
- **Substitution:**
  - `${name}` inside a string substitutes a variable; a string that is only `${name}` keeps the
    variable's type.
  - `"<file>#<key>"` reads a value from a JSON file (used to share physics settings with the
    exported parameter file).
  - There are no expressions. A conditional is two scenes or an include.
- **System entries** are a name or `{ "name", "params" }`. The suffix `@act` runs the system only on the
  first tick of a decision window (right after actions arrive); `@observe` only on its last tick (right
  before observations leave). See [simulation.md](simulation.md#per-tick-schedule). A world template may `replace` or `remove` systems by name, so variants stay
  small.
- **Entities** are a prefab plus component overrides. Components are keyed by registered name, and
  `set` patches a field path on every entity of a template.
- **Validation.** The loader fails, naming the file and JSON path, on:
  - an unknown key, system, component, prefab or variable;
  - a type mismatch;
  - a renderer block in `dynamical_train`;
  - a world system that touches the renderer.
- **The resolved scene is dumped** to `<log dir>/scene.resolved.json` at start. It is the
  provenance record of the run, next to `envparams.txt`.

## Registries

```cpp
// src/scene/system_registry.h
using SystemFactory = std::function<std::unique_ptr<System>(entt::registry&, const nlohmann::json& params)>;
struct SystemInfo { SystemFactory make; SystemScope scope; bool needs_renderer; };   // scope: Host, World, Both
void registerSystem(std::string name, SystemInfo);
// src/scene/component_registry.h
struct ComponentInfo {
    std::function<void(entt::registry&, entt::entity, const nlohmann::json&)> emplace;
    std::function<nlohmann::json(const entt::registry&, entt::entity)> save;   // for scene dumps and the inspector
};
void registerComponent(std::string name, ComponentInfo);
```

- `registerSimSystems()` (library), `registerViewSystems()` (`dynamical`) and `registerTrainSystems()`
  (`dynamical_train`) are called explicitly from each `main` (see
  [architecture.md](architecture.md#cmake)).
- Existing master systems keep their constructors. Their factory ignores `params` unless the system
  takes any.
- Components with a JSON form: `Transform`, `RigidBody`, `ModelRenderable` (by model path),
  `Ground`, `AgentSpawn`, `PoseTrackPlayer`.

## Prefabs

`resources/prefabs/<name>.json` holds a component map, optionally with `"extends"`. Prefabs can
expand into several entities: `humanbot` resolves to an `AgentSpawn` that the `AgentFactory` world
system expands into the ragdoll entities at world init.

## Runtime

```
main → JoltRuntime → registerSimSystems() + registerViewSystems() → Game(argc, argv)
Game::start
  scene = loadScene(settings.scene, overrides)       // include, substitute, validate, dump
  if scene.renderer: renderer = Renderer(reg, scene.renderer)
  host systems from scene.host
  for each world entry: count × World(template)       // registry, PhysicsWorld, clock, rng, systems, entities
  preinit / init (host, then worlds in index order)
  loop: renderer?.prepare → host pre → host post → renderer?.render
```

- `base` has no worlds. Its systems run on the host registry exactly as `Game::start` does today.
- In `dynamical_train`, the `TrainHost` loop replaces the frame loop ([simulation.md](simulation.md#training-host)).
- The renderer only reads the host registry. `WorldView` mirrors segment poses from worlds into host
  entities (`Transform` + `ModelRenderable`), applying a grid offset per world (sim and engine share one frame). So
  `ScenePass::record` keeps its single-registry, read-only rule.

## The four scenes

### `base` (dynamical)

A one-to-one port of today's `Game::start`:
- host pre: `Input, UI, Time, PlayerControl, VRControl, DebugMenu, ChunkManager, ChunkGenerator,
  BoxThrower, Physics`;
- host post: `Terrain`;
- passes: `terrain, grass, model, ui`, VR following `vr_mode`.

The ring of boxes and the glTF line-up from `SceneSys::init` become entities and a `ModelLineup`
spawner. **Gate:** looks and behaves as master.

### `rig_anim` (dynamical)

One world with the body built kinematic and a `PoseTrackPlayer`. There is no policy, trainer or
drives. See [rig-animation.md](rig-animation.md).

### `walker_train` (dynamical_train)

- `num_worlds` (default 25, matching Unity's 5×5 grid) copies of `humanbot_world` on flat ground.
- Host: only `TrainHost`. No renderer and no input.
- Variables: `num_worlds`, `params`, `threads`, `mode`.
- Unity's launch flags map onto settings, not variables ([mlagents-bridge.md](mlagents-bridge.md#launch)).

### `walker_view` (dynamical)

- The same world template with `TrainerActions` replaced by `Policy`.
- A `latest:` model source by default, or a fixed `.onnx` path.
- Desktop camera, `AgentPanel`, `WorldView`. It runs alongside a training process and shares only the
  results folder ([inference-and-replay.md](inference-and-replay.md#walker_view)).
