# Dynamical reference documentation

Reference docs for every system, component and source folder of the engine. Each statement was checked against the code at
the commit recorded below. If the code and these docs disagree, the code is right; fix the docs.

- **[Systems](systems/README.md)**: one page per system, scene pass or service. Each page covers purpose, lifecycle (params, preinit,
  init, tick, finish), expected behavior, the components and context variables it touches with their `read` / `write` /
  `readwrite` access, side effects, and ordering.
- **[Components](components/README.md)**: one page per type stored in a registry (entity component, tag or `reg.ctx()` variable). Each page
  covers fields, scene JSON, and **every system that touches it**, with access, phase, details and side effects per system.
- **[Access matrix](systems/access-matrix.md)**: a grid of systems against components.
- **[Folders](folders/README.md)**: one summary per source folder, covering its files, the systems and components defined there, and its subfolders.
  Start at [`src/`](folders/src.md).

Documented at commit `4af13d0`.

## Registries

- **Host registry**: owned by `Game` (`src/view/game.cpp`), used by `dynamical` only. Its Pre and Post lanes tick once per frame
  around the renderer. It holds the desktop camera, VR rig, chunks, models and the host-side physics.
- **World registries**: one per `World` (`src/scene/world.cpp`), each with its own Jolt `PhysicsWorld`, `WorldClock`, `WorldRng`
  and Fixed lane. They tick at the physics rate, gated by the decision window `Phase` (Every / Act / Observe) and the `Parked` flag.
  `dynamical_train` and the tests run worlds headless.
- **Scratch**: the scene loader validates components on a throwaway registry (`SceneLoader`).
- The registry `scope` in the scene registry is Host / World / Both. The lane a host system runs in is chosen by the scene file
  (`host.pre` / `host.post`). The lanes listed in these docs are the ones the shipped scenes use.

## Conventions

- Access values:
  - `read`: the system only reads.
  - `write`: the system writes without depending on the previous value.
  - `readwrite`: both.
  - `+create` / `+emplace_ctx` / `+destroy` / `+remove`: the system also emplaces the component, emplaces the context variable,
    destroys entities, or removes the component.
- Kinds:
  - `system`: a `dy::System` in a `SystemSet` lane.
  - `pass`: a renderer scene pass (`update` on the main thread, `record` concurrent).
  - `service`: other code that reads or writes registry state, such as `Game`, `Renderer`, `World`, `TrainHost`.
- File names: C++ qualifiers and template brackets become dashes, and `std::` is dropped. For example, `tf::Executor` →
  `tf-Executor.md` and `std::shared_ptr<TerrainGenerator>` → `shared_ptr-TerrainGenerator.md`.

## How the docs are produced and kept up to date

| Part | Written by | Source of truth |
|---|---|---|
| `systems/<Name>.md` | hand | code |
| `folders/<path>.md` | hand | code |
| `data/<area>.json` | hand | code: list of systems and components, plus every (system, component, access) entry |
| `components/*.md`, `systems/README.md`, `systems/access-matrix.md`, `folders/README.md` | `tools/gen_reference_docs.py` | `data/*.json` |

When code changes:
1. Update the affected system and folder docs.
2. Update the matching entries in `data/<area>.json`. The areas are `logic`, `physics` (with `onnx`), `view`, `renderer`, and `scene_train` (with `train`, `mlagents`, `util` and the repo-level folders).
3. Run `python3 tools/gen_reference_docs.py`. It regenerates the component docs and indexes.
4. Run `python3 tools/gen_reference_docs.py --check`. It exits non-zero on stale generated files, duplicate definitions, systems
   without a doc, unknown access values or broken relative links.
5. Bump the commit above.

The docs are plain Markdown with relative links only, so they can be exported as they are: copy the folder, or feed it to
MkDocs, Docusaurus, GitHub wiki or Pandoc. `data/*.json` is the machine-readable form of the access information.
