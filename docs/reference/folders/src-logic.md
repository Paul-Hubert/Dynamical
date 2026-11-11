# `src/logic/`

> [Source](../../../src/logic/) · Parent: `src/`

## Summary
Renderer-independent simulation code shared by `dynamical` (the desktop / VR viewer), `dynamical_train` and the tests: the `System` / `SystemSet` framework that both the host registry and every `World` registry tick, the plain-data components and context variables (transforms, rigid bodies, grounds, cameras, VR state, frame clock, render toggles), the marching cubes terrain data model and its density generator, the host chunk streaming systems, the `sphere_roll` agent (component, systems and the `AgentIO` contract with the training host) and the user `Settings`. Nothing here includes Vulkan; the agent code depends on `src/physics` (Jolt) and `src/scene` (registries of components and systems, `WorldRng`, `WorldClock`).

## Files
| File | Contents |
|---|---|
| None | This directory only holds subfolders. |

## Systems defined here
See the subfolders: [`systems/`](src-logic-systems.md) (BoxDrop, ChunkManager, ChunkGenerator, and the `SystemSet` service) and [`agents/`](src-logic-agents.md) (SphereControl, SphereTask, SphereObserve, TrainerActions, SphereHeuristic).

## Components defined here
See the subfolders: [`components/`](src-logic-components.md), [`systems/`](src-logic-systems.md) (Parked), [`agents/`](src-logic-agents.md), [`terrain/`](src-logic-terrain.md), [`settings/`](src-logic-settings.md).

## Subfolders
- [`src/logic/agents/`](src-logic-agents.md): the sphere_roll agent and `AgentIO`.
- [`src/logic/components/`](src-logic-components.md): shared components and context variables.
- [`src/logic/settings/`](src-logic-settings.md): `Settings` (config file + command line).
- [`src/logic/systems/`](src-logic-systems.md): `System`, `SystemSet`, `Phase`, `Parked`, BoxDrop and the chunk streaming systems.
- [`src/logic/terrain/`](src-logic-terrain.md): chunk data model, `TerrainGenerator`, CPU marching cubes.
