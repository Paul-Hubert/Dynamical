# DebugMenuSys

> **Kind:** system · **Registry name:** `DebugMenu` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/systems/system_list.h`](../../../src/view/systems/system_list.h), [`src/view/systems/debug_menu.cpp`](../../../src/view/systems/debug_menu.cpp)

## Purpose
The engine's ImGui window "Dynamical": frame time, camera position, chunk statistics, view distance and render toggles, physics statistics with a "Drop 50 boxes" button, grass shader parameters, and VR session state with a button that moves the play area to the camera.

## Lifecycle
- **Construction / params:** `DEFINE_SYSTEM(DebugMenuSys)`, no JSON params.
- **preinit:** None (empty).
- **init:** None (empty).
- **tick(dt):** gets `Settings`, `RenderSettings`, `Camera` (all required), then window "Dynamical" (first-use position (10, 10)):
  1. `dt` in ms and fps; camera position; a controls hint.
  2. Header "Terrain" (open by default): counts `view<ChunkC>().size()`, `view<ChunkGenerated>().size()`, `view<ChunkReady>().size()`; slider `Settings::view_distance` 256..2048; checkboxes `RenderSettings::terrain`, `grass`, `models`.
  3. Header "Physics" (open): if `PhysicsStats` exists shows bodies / active, terrain cells / triangles, step ms x steps, colliders ms; button "Drop 50 boxes": 50 `spawnBox` calls (size 1, orange shades) at `camera.position + forward() * 15 + offset`, offsets on a 5 x 5 grid of 1.5 m in x/z, 10 m up, a second layer 1.5 m higher for i >= 25.
  4. Header "Grass" (closed): sliders on `RenderSettings::grass_params` `tile_size` [0.01, 1], `height` [0.01, 5], `base_normal.xyz` [-1, 1], `noise_frequency` and `noise_amplitude` [0.0001, 1] logarithmic.
  5. Header "VR" (open): without `VRInput` or with `!enabled` shows "No OpenXR headset (vr_mode=N)"; else session running/idle and focused, `VRRig::position`, and a button that sets `Input::on[VR_RIG]`.
- **finish:** None (empty).

## Expected behavior
- `view_distance` changes are picked up by the chunk streaming system on later frames; they are not written to `config.6.json` (Settings saves only when loaded).
- The VR button does not move the rig itself: [PlayerControlSys](PlayerControlSys.md) consumes VR_RIG (next frame when it ticks before this system, as in the shipped scenes).
- Chunk counts are storage sizes (also count chunks being destroyed).
- Missing `Settings`, `RenderSettings` or `Camera` is a hard error (`get`).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Settings`](../components/Settings.md) | host | readwrite | tick | `view_distance` slider, `vr_mode` text |
| [`RenderSettings`](../components/RenderSettings.md) | host | readwrite | tick | toggles and `grass_params` |
| [`Camera`](../components/Camera.md) | host | read | tick | `position`, `forward()` |
| [`ChunkC`](../components/ChunkC.md) | host | read | tick | storage size |
| [`ChunkGenerated`](../components/ChunkGenerated.md) | host | read | tick | storage size |
| [`ChunkReady`](../components/ChunkReady.md) | host | read | tick | storage size |
| [`PhysicsStats`](../components/PhysicsStats.md) | host | read | tick | `find`, all fields |
| [`VRInput`](../components/VRInput.md) | host | read | tick | `enabled`, `session_running`, `focused` |
| [`VRRig`](../components/VRRig.md) | host | read | tick | `position` |
| [`Input`](../components/Input.md) | host | write | tick | sets `on[VR_RIG]` |
| [`ModelManager`](../components/ModelManager.md) | host | read | tick | spawnBox |
| [`Transform`](../components/Transform.md) | host | write +create | tick | spawnBox |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | write +create | tick | spawnBox |
| [`RigidBody`](../components/RigidBody.md) | host | write +create | tick | spawnBox, Dynamic boxes |

## Side effects
- ImGui window "Dynamical".
- Creates 50 host entities per button press (Jolt bodies follow in the physics systems).

## Ordering and dependencies
- After [UISys](UISys.md) (ImGui frame) and [PlayerControlSys](PlayerControlSys.md) (`Camera` emplaced in preinit).
- `RenderSettings` and `VRRig` come from the renderer; `PhysicsStats` from the host physics system (optional).
- [SphereViewSys](SphereViewSys.md) collapses this window on first use.

## Related
[PlayerControlSys](PlayerControlSys.md), [BoxThrowerSys](BoxThrowerSys.md), [folder `src/view/systems/`](../folders/src-view-systems.md).
