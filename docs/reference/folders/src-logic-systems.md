# src/logic/systems

The system base class and the set that runs systems by lane, plus the chunk streaming systems and the box drop
of the test worlds.

| File | What |
|---|---|
| [`system.h`](../../../src/logic/systems/system.h) | `System` (a registry reference; virtual `preinit`, `init`, `tick(dt)`, `finish`, `name`) and `Phase` (`Every`, `Act`, `Observe`: when a world system runs in a decision window) |
| [`system_set.h`](../../../src/logic/systems/system_set.h) / [`system_set.cpp`](../../../src/logic/systems/system_set.cpp) | `SystemSet`: `Pre`, `Post`, `Fixed` lists, preinit / init / finish of all, `pre_tick`, `post_tick`, `fixed_tick(h, act, observe)` with phases and `Parked`, `fixed_observe_only`, profiler zones per system; `Parked` world flag |
| [`chunk_systems.h`](../../../src/logic/systems/chunk_systems.h) / [`chunk_systems.cpp`](../../../src/logic/systems/chunk_systems.cpp) | `ChunkManagerSys` (chunk entities around the camera and the VR rig, `ChunkDestroying` for far ones) and `ChunkGeneratorSys` (densities on the executor, 16 jobs at most, synchronous in fixed-clock runs) |
| [`box_drop.h`](../../../src/logic/systems/box_drop.h) / [`box_drop.cpp`](../../../src/logic/systems/box_drop.cpp) | `BoxDropSys`: boxes and capsules dropped with the world RNG (test_boxes) |

## Systems registered from here

(Registered in [`src/scene/system_registry.cpp`](../../../src/scene/system_registry.cpp).)

- [ChunkManager](../systems/ChunkManager.md) (`ChunkManagerSys`, host)
- [ChunkGenerator](../systems/ChunkGenerator.md) (`ChunkGeneratorSys`, host)
- [BoxDrop](../systems/BoxDrop.md) (`BoxDropSys`, world)

How the set runs systems: [service-Game](../systems/service-Game.md).

## Components and context defined here

- [Parked](../components/Parked.md): world context flag, a killed agent waits for the end of its decision window.

## Main classes

- `System`: every system of the engine; `DEFINE_SYSTEM` (`system_list.h` elsewhere) for the trivial ones.
- `SystemSet::add(list, system, phase, name)`: `name` is the scene's registry name, used for the profiler zones.

## Dependencies

[`src/logic/terrain`](src-logic-terrain.md) (chunks, `TerrainGenerator`), [`src/logic/components`](src-logic-components.md)
(`Camera`, `VRInput`, `VRRig`, `Transform`, `RigidBody`, `ModelRef`), [`src/logic/settings`](src-logic-settings.md)
(`Settings`), [`src/scene`](src-scene.md) (`RunArgs`, `WorldRng`), [`src/util`](src-util.md) (profiler), taskflow.

## Design docs

[docs/physical-agents/simulation.md](../../physical-agents/simulation.md) (lanes, phases, parking),
[docs/port-mc-grass-gltf-vr-jolt.md](../../port-mc-grass-gltf-vr-jolt.md) (chunks).
