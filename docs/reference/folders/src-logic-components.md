# src/logic/components

Plain data shared by the simulation and the viewer: transforms, rigid bodies and grounds, models and lights by
reference, the cameras and the headset's state, the frame clock, and the settings and statistics the renderer and
the debug menu read. Headers only.

| File | What |
|---|---|
| [`transform.h`](../../../src/logic/components/transform.h) | `Transform`: position, rotation, scale, `matrix()` |
| [`rigid_body.h`](../../../src/logic/components/rigid_body.h) | `RigidBody` (motion, shape, sizes, material, mass, lock rotation, initial velocities, Jolt body id), `Ground` (static box), `HeightField` (static rolling hills from plane waves), context `PhysicsStats` |
| [`model_ref.h`](../../../src/logic/components/model_ref.h) | `ModelRef`: a glTF model by path under `resources/` and a colour; `dynamical` resolves it into a `ModelRenderable` |
| [`point_light.h`](../../../src/logic/components/point_light.h) | `PointLight`: a torch next to a `Transform` (colour, intensity at 1 m, radius) |
| [`camera.h`](../../../src/logic/components/camera.h) | `Camera`: the desktop free camera (context), view and Vulkan projection |
| [`vr.h`](../../../src/logic/components/vr.h) | `VRRig` (play area in the world, context), `VRPose`, `VRInput` (headset and controllers in stage space, context) |
| [`frame_clock.h`](../../../src/logic/components/frame_clock.h) | `FrameClock`: dt, time, frame of the main loop (context) |
| [`render_settings.h`](../../../src/logic/components/render_settings.h) | `RenderSettings`: toggles and parameters of the scene passes, lighting tiers per target, textures, streaming, soil, vegetation and grass (context) |
| [`lighting_stats.h`](../../../src/logic/components/lighting_stats.h) | `LightingStats`: what the ray traced lighting still builds, `settled()` (context) |

## Systems registered here

None. The systems that touch them are listed on each component page; in this group:
[RigidBody](../systems/RigidBody.md), [Physics](../systems/Physics.md), [PhysicsStep](../systems/PhysicsStep.md),
[TerrainColliders](../systems/TerrainColliders.md), [WorldTerrain](../systems/WorldTerrain.md),
[BoxDrop](../systems/BoxDrop.md), [ChunkManager](../systems/ChunkManager.md), [ChunkGenerator](../systems/ChunkGenerator.md).

## Components and context defined here

- Components: [Transform](../components/Transform.md), [RigidBody](../components/RigidBody.md), [Ground](../components/Ground.md), [HeightField](../components/HeightField.md), [ModelRef](../components/ModelRef.md), [PointLight](../components/PointLight.md)
- Context: [PhysicsStats](../components/PhysicsStats.md), [Camera](../components/Camera.md), [VRRig](../components/VRRig.md), [VRInput](../components/VRInput.md), [FrameClock](../components/FrameClock.md), [RenderSettings](../components/RenderSettings.md), [LightingStats](../components/LightingStats.md)

JSON names (scenes, prefabs): `Transform`, `RigidBody`, `Ground`, `HeightField`, `PointLight`, `ModelRef`
(registered in [`src/scene/system_registry.cpp`](../../../src/scene/system_registry.cpp)).

## Main functions

- `Camera::forward`, `getView`, `getProjection(aspect)` (Vulkan clip space: Y flipped, depth 0..1).
- `RenderSettings::Lighting::of(tier)`: the values of a lighting tier.
- `HeightField::height(x, z)` (defined in `src/physics/height_field.cpp`).

## Dependencies

glm; `render_settings.h` includes [`src/logic/grass`](src-logic-grass.md) (`GrassHeightParams`, `GrassSpeciesParams`)
and [`src/logic/terrain`](src-logic-terrain.md) (`SoilParams`).

## Design docs

[docs/physical-agents/physics.md](../../physical-agents/physics.md) (bodies, grounds), [docs/lighting.md](../../lighting.md)
(`RenderSettings::lighting`, `LightingStats`), [docs/graphics.md](../../graphics.md), [docs/grass.md](../../grass.md), [docs/ground.md](../../ground.md).
