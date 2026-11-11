# Probe

> **Kind:** service · **Registry name:** — · **Scope:** Host · **Lane:** per-frame call from `Game::start`, after `post_tick` and before `render` · **Needs renderer:** no
> **Source:** [`src/scene/probe.h`](../../../src/scene/probe.h), [`src/scene/probe.cpp`](../../../src/scene/probe.cpp)

## Purpose
Behaviour probe for the determinism and regression tests (`S0.1`, `S1.1` in `CMakeLists.txt`, golden `tests/golden/base_probe.csv`): every `probe_every` frames it writes one CSV row per host-registry entity holding the probed component, ordered by entity id, with the entity's `Transform` position and rotation.

## Lifecycle
- **Construction / params:** `Probe(const RunArgs& args)`: opens `args.probe` for writing (binary, truncating) in the member initialiser, stores `probe_entities` and `probe_every`. Then: if the component is neither `RigidBody` nor `Transform`, sets `error` ("only RigidBody and Transform can be probed") and returns (the file has already been created); if the file did not open, sets `error` ("cannot write probe file ..."); otherwise writes the header `frame,index,entity,x,y,z,qw,qx,qy,qz`. `Game::start` logs `error` and exits 1 when `failed()`.
- **preinit:** None.
- **init:** None.
- **tick(dt):** `sample(const entt::registry& reg, uint64_t frame)`: returns when `failed()` or `frame % every != 0`. Collects `view<Transform, RigidBody>` (component `RigidBody`) or `view<Transform>` (component `Transform`), sorts by entity id, and writes for each rank `i`: `frame, i, entity id, position.x/y/z, rotation.w/x/y/z`, floats as `%.9g`. Flushes after each sample.
- **finish:** None (file closed with the object).

## Expected behavior
- Rows appear only on frames that are multiples of `probe_every` (frame counter of `FrameClock`, starting at 1 in the first loop iteration, so the first sample is frame `probe_every`).
- Row order is stable for a given entity creation order; `index` is the rank in that order, `entity` the raw `entt` id.
- Only the host registry is probed: entities of `Worlds` are not sampled.
- Two runs with a fixed frame clock (`probe=` implies `fixed_frame_dt=1/60`) produce identical files.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [Transform](../components/Transform.md) | host | read | sample | position, rotation |
| [RigidBody](../components/RigidBody.md) | host | read | sample | membership filter only (no field read) |

## Side effects
- Creates or truncates the CSV file at construction; appends and flushes rows each sampled frame.

## Ordering and dependencies
- Called by `Game::start` after both host lanes and before rendering, so it sees the transforms written this frame (interpolated by `PhysicsSys`).
- Main thread only; reads through `const entt::registry&`.

## Related
[World](World.md), `tools/visual_test.py probe`, [folder `src/scene/`](../folders/src-scene.md), [RunArgs component](../components/RunArgs.md).
