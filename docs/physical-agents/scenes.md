# Scenes

A scene is a JSON file `resources/scenes/<name>.scene.json` that defines one way of running the
engine:
- its variables;
- whether it renders;
- the host systems;
- N worlds with their systems and content.

Shared fragments live in `resources/scenes/common/*.json` and are only used through `include`. Both
executables load scenes with the same loader: `dynamical scene=<name>` and `dynamical_train scene=<name>`.

## File format

```jsonc
// resources/scenes/walker_view.scene.json
{
  "format_version": 1,
  "name": "walker_view",
  "include": ["common/humanbot_world.json", "common/desktop.json"],   // merged in order, this file last

  "variables": {
    "num_worlds": { "type": "int",    "default": 1 },
    "run":        { "type": "string", "default": "" },
    "policy":     { "type": "string", "default": "latest:results/${run}/HeidelBotBehavior" },
    "replay":     { "type": "string", "default": "" },
    "params":     { "type": "string", "default": "agents/params/walker.params.json" }
  },

  "renderer": { "passes": ["model", "ui"], "vr": false },      // omitted = no renderer (refused by dynamical_train)

  "host": {
    "pre":  ["AgentPanel", { "name": "ModelWatcher", "params": { "source": "${policy}" } },
             { "name": "WorldStepper", "params": { "realtime": true } }],
    "post": [{ "name": "WorldView", "params": { "worlds": "all", "spacing": 6.0 } }]
  },

  "worlds": [
    { "template": "humanbot_world", "count": "${num_worlds}",
      "systems": { "replace": { "TrainerActions@act": "Policy@act" } } }
  ]
}
```

### World templates

```jsonc
// resources/scenes/common/humanbot_world.json
{
  "templates": {
    "humanbot_world": {
      "physics": { "threads": 1, "max_bodies": 128, "gravity": [0, -9.81, 0], "substeps": "${params}#substeps" },
      "clock":   { "physics_hz": "${params}#physics_hz", "control_hz": "${params}#control_hz" },
      "fixed": [
        "TrainerActions@act", "DriveTargets", "Disturbances", "PhysicsStep", "BodySync",
        "Contacts", "Energy", "Task", "Stats", "Termination",
        "Rewards@observe", "Observation@observe", "Episode@observe"
      ],
      "entities": [
        { "prefab": "ground_flat" },
        { "prefab": "humanbot", "AgentSpawn": { "body": "agents/bodies/heidelbot_notoes.body.json", "params": "${params}" } }
      ]
    }
  }
}
```

```jsonc
// resources/scenes/common/desktop.json: host systems of every rendered scene, before the scene's own
{ "host": { "pre": ["Input", "UI", "PlayerControl", "DebugMenu"] } }
```

`PlayerControl` creates the `Camera` ctx that `ClassicRender` requires (`classic_render.cpp:76`), so
every scene with a renderer must include `common/desktop.json`. The loader checks this.

### Rules

- **Variables** have a type and a default. Only the command line overrides them
  (`var.<name>=<value>`), last wins.
  - ML-Agents environment parameters are **not** scene variables: they belong to the agent parameters
    and follow Unity's precedence ([mlagents-bridge.md](mlagents-bridge.md#environment-parameters)).
  - Process settings (`mode`, `threads`, `capture`, `-logFile`, …) are **run arguments**, not
    variables ([architecture.md](architecture.md#run-arguments)).
- **Substitution:**
  - `${name}` inside a string substitutes a variable; a string that is only `${name}` keeps the
    variable's type.
  - `"<file>#<key>"` reads a value from a JSON file, for example the physics rate from the params file.
  - There are no expressions. A conditional is two scenes or an include.
- **System entries** are a registered name with an optional phase suffix, or `{ "name", "params" }`.
  - `@act` runs the system on the first tick of a decision window (right after actions arrive).
  - `@observe` runs it on the last tick (right before observations leave).
  - See [simulation.md](simulation.md#per-tick-schedule).
  - A world entry may `replace` or `remove` systems of its template.
- **Variables of a world entry** (T1.5). `"variable_files": ["common/wheel_body.json"]` applies those files'
  variable defaults, then `"variables": { "radius": 0.94 }` sets values over them. Both apply to that entry's worlds
  only, and win over the scene's values and the command line. The entry's template is resolved again from its text
  as written. The resolved scene holds it as a template of its own, `<template>@<entry>`, so the dump reloads as
  is; `WorldDef::template_name` stays `<template>`. `vr_roller` uses it for the desktop player's wheel.
- **Entities** are a prefab plus component overrides. Components are keyed by registered name.
- **Validation.** The loader fails, naming the file and JSON path, on:
  - an unknown key, system, component, prefab or variable;
  - a type mismatch;
  - a renderer block in `dynamical_train`;
  - a rendered scene without `common/desktop.json`;
  - a world system used at host scope, or the reverse.
- **The resolved scene is dumped** to `<log dir>/scene.resolved.json` at start. It is the provenance
  record of the run, next to `envparams.txt`.
- **`ScenarioRunner`** (tests, `dynamical_train`) loads a scene, **ignores its `renderer` and `host`
  blocks**, and steps its worlds. A world template that only uses library systems therefore runs
  headless, unchanged.

## Registries

```cpp
// src/scene/system_registry.h
using SystemFactory = std::function<std::unique_ptr<System>(entt::registry&, const nlohmann::json& params)>;
struct SystemInfo { SystemFactory make; SystemScope scope; bool needs_renderer; };   // scope: Host, World, Both
void registerSystem(std::string name, SystemInfo);
// src/scene/component_registry.h
struct ComponentInfo {
    std::function<void(entt::registry&, entt::entity, const nlohmann::json&)> emplace;
    std::function<nlohmann::json(const entt::registry&, entt::entity)> save;   // scene dumps, inspector
};
void registerComponent(std::string name, ComponentInfo);
```

- Registration is explicit, called from each `main` and the test main
  ([architecture.md](architecture.md#cmake)):
  - `registerSimSystems()` in the library;
  - `registerViewSystems()` in `dynamical`;
  - `registerTrainSystems()` in `dynamical_train`.
- **Registry names are independent of `System::name()`.** `DEFINE_SYSTEM` returns `"TimeSys"`, while the
  registry name is `Time`. Tests compare registry names.
- **Components with a JSON form:**
  - library: `Transform`, `RigidBody`, `Ground`, `AgentSpawn`, `PoseTrackPlayer`, and
    `ModelRef { path, color }`;
  - `dynamical` only: `ModelRenderable`. A view-side system resolves `ModelRef` into `ModelRenderable`,
    because `ModelRenderable` needs the renderer's `ModelManager` (`renderer.cpp:39`) and has no
    default constructor.

### Canonical system names

| Registry name | Class / file | Scope | Lib or exe | Phase | First used |
|---|---|---|---|---|---|
| `Input`, `UI`, `PlayerControl`, `VRControl`, `DebugMenu`, `Time` | master systems | host | `dynamical` | pre | P1 |
| `ChunkManager`, `ChunkGenerator` | `chunk_systems.*` | host | lib | pre | P1 |
| `BoxRing`, `ModelLineup`, `BoxThrower` | replace `SceneSys` (`src/view/`); `spawnBox` stays view-side, shared with `VRControl` | host | `dynamical` | pre | P1 |
| `Physics` | master `PhysicsSys` (accumulator, interpolation) | host | lib | pre | P1. Split at P2: `RigidBody`, `TerrainColliders`, `Physics` |
| `Terrain` | `TerrainSys` | host | `dynamical` | post | P1 |
| `WorldStepper`, `WorldView` | `src/view/` | host | `dynamical` | pre / post | P2 |
| `TrainHost` | `src/train/` | host | `dynamical_train` | loop | P2 |
| `ModelWatcher`, `AgentPanel`, `RigPanel` | `src/view/`, `src/inference/` | host | `dynamical` | pre | P11, P8, P4 |
| `RigidBody` | create and destroy `RigidBody` bodies | world or host | lib | every | P2 |
| `PhysicsStep` | `n × Update(h/n, 1)`, calls the contact and energy recorders after each sub-step | world | lib | every | P2 |
| `AgentFactory` | builds ragdoll slots from `AgentSpawn` (init only) | world | lib | init | P4 |
| `BodySync` | segment states, anchor finite differences, joint angles | world | lib | every | P4 |
| `TrackActions` | plays a `PoseTrack` kinematically (`MoveKinematic` or teleport) | world | lib | every | P5 |
| `RigChecks` | limits, bone lengths, velocities, ground (geometric) | world | lib | every | P5 |
| `GroundPlane`, `GroundTerrain` | the world's `GroundField` ([ik-gait.md](ik-gait.md)) | world | lib | preinit | after P13 |
| `GaitBase`, `GaitFootIK`, `GaitBalance`, `GaitChecks` | the kinematic walk: base animation, foot targets, pelvis placement, measurements | world | lib | every | after P13 |
| `Fabrik`, `RigPoseApply` | solve every `IKTarget` into `RigPose`; `RigPose` onto the kinematic bodies | world | lib | every | after P13 |
| `GaitView` | follow camera, panel and markers of the `ik_walk` scenes | host | `dynamical` | pre | after P13 |
| `DriveTargets` | held targets → Jolt motors | world | lib | every | P6 |
| `Contacts`, `Energy` | aggregate the per-sub-step recorders per tick | world | lib | every | P7 |
| `TrainerActions`, `Policy`, `HeuristicActions` | actions from the bridge / ONNX (through `PolicyHandle`) / random or zero → `HeldDrive` | world | lib | act | P8, P11, P8 |
| `Disturbances`, `Task`, `Stats`, `Termination` | push/pull; target and speed; stats; kill and truncation | world | lib | every | P8 |
| `Observation`, `Rewards`, `Episode` | observation stack; window rewards; outbox and reset | world | lib | observe | P8 |

## Prefabs

`resources/prefabs/<name>.json` holds a component map, optionally with `"extends"`. `humanbot`
resolves to an `AgentSpawn { body, params, motion }` that `AgentFactory` expands into the ragdoll
entities at world init.

## Runtime order

`Game::start` keeps master's interleaving (`game.cpp:43-77`):

```
main → JoltRuntime → register*Systems() → Game(argc, argv)          // RunArgs parsed; Settings only in dynamical
Game::start
  scene = loadScene(args.scene, args.vars)                          // include, substitute, validate, dump
  if scene.renderer: renderer = Renderer(reg, scene.renderer)       // creates ModelManager, RenderSettings, the listed passes
  host systems from scene.host; worlds from scene.worlds            // World: registry, PhysicsWorld, clock, rng, SystemSet, entities
  renderer?.preinit → host preinit → worlds preinit (index order)
  host init → worlds init → renderer?.init
  loop: renderer?.prepare → host pre → host post → renderer?.render
```

- `base` has no worlds. Its systems run on the host registry as in `Game::start` today.
- In `dynamical_train`, the `TrainHost` loop replaces the frame loop ([simulation.md](simulation.md#training-host)).

## `WorldView` (drawing worlds)

The renderer only reads the host registry. `WorldView` (host post) mirrors world bodies into host
entities, so `ScenePass::record` keeps its single-registry, read-only rule.

- **One host entity per sub-shape** of every segment body (compound shapes give several). Each one gets:
  - a `Transform` = world offset × body transform × sub-shape local transform;
  - a `ModelRenderable`.
- **Meshes.**
  - Boxes and spheres use unit primitives in `resources/models/primitives/` (outside `ModelLineup`'s
    folder, so `base` does not change), scaled.
  - **Capsules are generated per (radius, half-height)** as a mesh and cached in `ModelManager` by
    key. Scaling a unit capsule would distort the caps.
  - Mesh axis = Jolt's capsule axis (local Y).
- **Lifetime.** Host entities are created on the first frame a world body exists and destroyed with it.
  World resets reuse bodies, so the entities persist.
- **Offsets.** World *i* is drawn at `offset = grid(i) · spacing` on the XZ plane.
- **Interpolation.** Dynamic bodies are drawn at `mix(prev, current, alpha)` from `WorldStepper`.
  Kinematic tracks are drawn at the current pose.
- **Colours:** by segment (default), by joint torque or contact (panel option), and red for segments
  outside a joint limit in `rig_anim`.
- **Overlays** (debug lines through the UI pass): bone axes, joint arcs with limit wedges,
  ground-reaction arrows, centre of pressure.

## Scenes

| Scene | Executable | Purpose | Phase |
|---|---|---|---|
| `base` | `dynamical` | Master's world. Host pre: `Input, UI, Time, PlayerControl, VRControl, DebugMenu, ChunkManager, ChunkGenerator, BoxRing, ModelLineup, BoxThrower, Physics`; post: `Terrain`; passes: terrain, grass, model, ui | P1 (the physics split changes its list at P2) |
| `test_boxes` | `dynamical_train` | 25 worlds × 50 boxes on flat ground | P2 |
| `test_boxes_view` | `dynamical` | 4 of those worlds, a fixed camera | P2 |
| `null` | `dynamical_train` | No worlds. `TrainHost mode=null` | P3 |
| `rig_anim` | `dynamical` | `common/rig_world.json` + desktop + `RigPanel` + `WorldView` ([rig-animation.md](rig-animation.md)) | P4–P5 |
| `rig_check` | tests (`ScenarioRunner`) | `common/rig_world.json` only, headless | P5 |
| `joint_lab` | `dynamical` | One joint per world: sweep, step and ramp modes | P6 |
| `physics_lab` | `dynamical` | Drop, quiet stance, slide, pendulum, with force overlays | P7 |
| `walker_heuristic` | `dynamical` | `humanbot_world` with random or zero actions (`TrainerActions` replaced by `HeuristicActions`) | P8 |
| `walker_train` | `dynamical_train` | `num_worlds` (default 25) × `humanbot_world`; host: `TrainHost` | P8 |
| `walker_demo` | `dynamical_train` | Kinematic body, `TrackActions`, `Recording`; variable `track` | P9 |
| `walker_view` | `dynamical` | `humanbot_world` with `Policy`; `ModelWatcher`; variables `run`, `policy`, `replay` | P11 |
| `walker_eval` | `dynamical_train` | `humanbot_world` with `Policy`; variables `policy`, `episodes` | P11 |
| `devita_train` / `devita_view` | as walker | H173 body, DeVita params, slope ground | P13 |
| `ik_walk_slope` / `ik_walk_terrain` | `dynamical` | `common/ik_world.json`: the kinematic walk on a tilted plane or on the marching cubes terrain, with `GaitView` ([ik-gait.md](ik-gait.md)) | after P13 |
| `ik_check` / `ik_check_terrain` | tests (`ScenarioRunner`) | The same worlds, headless | after P13 |

Capture scripts live in `tests/capture/<name>.capture.json` ([phases.md](phases.md#capture-facility-p0)).
