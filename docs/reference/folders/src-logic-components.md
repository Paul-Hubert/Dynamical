# `src/logic/components/`

> [Source](../../../src/logic/components/) · Parent: [`src/logic/`](src-logic.md)

## Summary
Plain-data types stored in the registries: per-entity components used by both the host and world registries (`Transform`, `RigidBody`), world-only scene components (`Ground`, `HeightField`, `ModelRef`), and context variables of the host registry (`Camera`, `FrameClock`, `RenderSettings`, `VRRig`, `VRInput`) or of both (`PhysicsStats`). They carry no behaviour beyond small helpers (matrices, camera vectors, `created()`); the systems in `src/physics`, `src/view` and `src/renderer` own their lifecycle. Scene JSON serialisation of `Transform`, `RigidBody`, `Ground`, `HeightField` and `ModelRef` lives in `src/scene/system_registry.cpp` (`registerSimComponents`).

## Files
| File | Contents |
|---|---|
| [`camera.h`](../../../src/logic/components/camera.h) | `Camera`: desktop free camera (position, yaw, pitch, fov, planes), `forward()`, `right()`, `getView()`, Vulkan-clip `getProjection(aspect)`. |
| [`frame_clock.h`](../../../src/logic/components/frame_clock.h) | `FrameClock`: dt, time, frame number of the dynamical frame loop. |
| [`model_ref.h`](../../../src/logic/components/model_ref.h) | `ModelRef`: glTF path under resources/ plus RGBA colour (only the colour is read today, by WorldView). |
| [`render_settings.h`](../../../src/logic/components/render_settings.h) | `RenderSettings`: terrain / grass / models pass toggles and `Grass` shader parameters. |
| [`rigid_body.h`](../../../src/logic/components/rigid_body.h) | `RigidBody` (Motion, Shape, sizes, material, initial velocities, Jolt BodyID), `Ground` (flat static box), `HeightField` (wave hills; `height()` in src/physics/height_field.cpp), `PhysicsStats`. |
| [`transform.h`](../../../src/logic/components/transform.h) | `Transform`: position, rotation, scale, `matrix()` = T R S. |
| [`vr.h`](../../../src/logic/components/vr.h) | `VRRig` (stage placement, `transform()`, `stick_locomotion`), `VRPose`, `VRInput` (head, two `Hand`s, `left` / `right` indices). |

## Systems defined here
None.

## Components defined here
- [Transform](../components/Transform.md)
- [RigidBody](../components/RigidBody.md)
- [Ground](../components/Ground.md)
- [HeightField](../components/HeightField.md)
- [PhysicsStats](../components/PhysicsStats.md)
- [ModelRef](../components/ModelRef.md)
- [Camera](../components/Camera.md)
- [FrameClock](../components/FrameClock.md)
- [RenderSettings](../components/RenderSettings.md)
- [VRRig](../components/VRRig.md)
- [VRInput](../components/VRInput.md)

## Subfolders
None.
