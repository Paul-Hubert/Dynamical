# DebugMenu

`DebugMenuSys` in [`src/view/systems/debug_menu.cpp`](../../../src/view/systems/debug_menu.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre (every desktop scene, `common/desktop.json`) · Parameters: no

The "Dynamical" ImGui window: frame time and camera position, the graphics level, and panels that edit the
[RenderSettings](../components/RenderSettings.md) live (terrain, soil, sky, lighting, grass, vegetation, animals),
show the statistics the renderer and the simulation publish, and a few buttons (torches, boxes, VR play area).

## Behaviour

**tick** (preinit, init, finish do nothing), one window at (10, 10) on first use:

- Header: ms / fps, camera position, the controls of PlayerControl.
- **Graphics** combo (when `Graphics` has presets): picking a level sets `Graphics::current`, calls
  `applyGraphicsPreset(preset, render)` and `Settings::view_distance = preset.view_distance`. Ray tracing and texture
  anisotropy only change at start.
- **Terrain**: counts of `ChunkC`, `ChunkGenerated`, `ChunkReady`; view distance slider (256..2048);
  `render.terrain`, `grass`, `models`, `soil` checkboxes.
- **Soil**: `soil_params` (cover and wavelength of mud, stones, sand; octaves; gain; edge width; grass margin),
  `render.scree`, `render.relief`. SoilMaps bakes the chunk maps again when they change.
- **Sky** (with `Sky`): time of the day, day length, declination; "Torch here" makes an entity with `Transform` 1.5 m
  ahead of the camera and a default `PointLight`; "No torches" destroys every entity with a `PointLight`.
- **Lighting** (with `LightingStats`): ray scene and probe statistics; probe rays a frame, rays a probe, far plants in
  the rays / from their volume; a tier combo per target (`RenderSettings::Lighting::of(tier)`).
- **Physics**: `PhysicsStats` when present; "Drop 50 boxes": 50 `spawnBox` of 1 m in a 5 x 5 x 2 block 15 m ahead
  and 10 m up.
- **Grass** (`grass_params`): tile size, height, layers, height map, patches, wind, colours, species, shadow, canopy,
  silhouette, dither, noise. Height-map and species changes make GrassPass bake its maps again.
- **Vegetation** (with `VegetationStats`): `render.vegetation`, normal maps, subsurface, cull, tree shade, LOD bias,
  forced LOD.
- **Animals** (with `AnimalStats`): statistics, `HerdStats` when present, `render.animals`; emplaces
  `GaitStyleOverride` if missing and edits it ("One walk for all" and the 13 `GaitStyle` sliders).
- **VR**: without an enabled `VRInput`, "No OpenXR headset (vr_mode=...)"; else session state, play area position, and
  a button that sets `Input::VR_RIG`.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Settings](../components/Settings.md) | context | readwrite | tick | view_distance slider and graphics level; vr_mode shown |
| [RenderSettings](../components/RenderSettings.md) | context | readwrite | tick | terrain/grass/models/soil/vegetation/animals toggles, soil, grass, lighting and vegetation parameters; applyGraphicsPreset |
| [Camera](../components/Camera.md) | context | read | tick | position shown; where torches and dropped boxes go |
| [Graphics](../components/Graphics.md) | context | readwrite | tick | the Graphics combo: current preset, applied to RenderSettings and Settings::view_distance |
| [ChunkC](../components/ChunkC.md) | component | read | tick | count of loaded chunks |
| [ChunkGenerated](../components/ChunkGenerated.md) | tag | read | tick | count of generated chunks |
| [ChunkReady](../components/ChunkReady.md) | tag | read | tick | count of meshed chunks |
| [Sky](../components/Sky.md) | context | readwrite | tick | time of the day, day_length, declination sliders; skyLight() for sun or moon |
| [PointLight](../components/PointLight.md) | component | readwrite+create+destroy | tick | counted; "Torch here" makes one 1.5 m ahead of the camera; "No torches" destroys every entity with one |
| [Transform](../components/Transform.md) | component | write+create | tick | of a new torch and of the dropped boxes (removed with the torches' entities) |
| [LightingStats](../components/LightingStats.md) | context | read | tick | ray tracing statistics shown |
| [PhysicsStats](../components/PhysicsStats.md) | context | read | tick | bodies, terrain colliders, step times shown |
| [VegetationStats](../components/VegetationStats.md) | context | read | tick | plants, species loaded / loading shown |
| [AnimalStats](../components/AnimalStats.md) | context | read | tick | animals, species shown |
| [HerdStats](../components/HerdStats.md) | context | read | tick | herds, travelling, moving shown |
| [GaitStyleOverride](../components/GaitStyleOverride.md) | context | readwrite+create | tick | emplaced when the Animals header is drawn; "One walk for all" and the GaitStyle sliders |
| [VRInput](../components/VRInput.md) | context | read | tick | enabled, session_running, focused shown |
| [VRRig](../components/VRRig.md) | context | read | tick | play area position shown |
| [Input](../components/Input.md) | context | write | tick | "Move play area to camera" sets Input::VR_RIG (PlayerControl acts on it) |
| [ModelManager](../components/ModelManager.md) | context | readwrite | tick (spawnBox) | box.gltf for the dropped boxes |
| [ModelC](../components/ModelC.md) | component | write+create | tick (spawnBox, ModelManager::load) | the box model on its first load |
| [ModelRenderable](../components/ModelRenderable.md) | component | write+create | tick (spawnBox) | the dropped boxes |
| [RigidBody](../components/RigidBody.md) | component | write+create | tick (spawnBox) | "Drop 50 boxes": 50 dynamic 1 m boxes 15 m ahead of the camera |

## Side effects

- One ImGui window "Dynamical" (needs UI before it).
- Changing the soil or grass height-map parameters makes SoilMaps / GrassPass bake their per-chunk maps again (they watch RenderSettings).
- Creates torch entities and dynamic boxes (Jolt bodies through RigidBodySys) on button presses; destroys every PointLight entity on "No torches".

## Order and dependencies

- After UI (ImGui frame). Needs `Settings` (Game), `RenderSettings` (renderer) and `Camera` (PlayerControl's preinit):
  `get` would fail without them.
- The VR button acts through PlayerControl (which reads `Input::VR_RIG`) the next time it ticks.
- Systems that open their own windows later in the frame may collapse it (SphereView).

## Used in scenes

<!-- generated -->

## Tests

- `T2.4 base system list ...` (stub). GPU: `P1 check: scripted input flies the camera, throws a box and toggles the grass`
  (clicks the Grass checkbox), base captures.
