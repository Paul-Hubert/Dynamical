# Renderer

`Renderer` in [`src/renderer/renderer.cpp`](../../../src/renderer/renderer.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by `Game`, [`src/view/game.cpp`](../../../src/view/game.cpp)) · Parameters: yes (the scene's `renderer` block)

The renderer of the viewer (`dynamical`): it owns the Vulkan context, the two render targets (the desktop window,
[ClassicRender](service-ClassicRender.md), and the headset, [VRRender](service-VRRender.md)), the shared GPU state
of the frame (the lights, the shade of the plants, the soil maps, and with ray tracing the ray scene, the probes
and the plant volumes) and the scene passes the scene lists. It is not a registered system: `Game::start` makes it
before the `SystemSet`, calls `preinit()` / `init()` around the systems' own, `prepare()` before the pre tick and
`render()` after the post tick, and `finish()` when the game ends.

## Behaviour

- **Constructor** (`Game::start`, before any system is made):
  1. Builds the [Context](service-Context.md) (OpenXR, SDL window, Vulkan instance, device + VMA, transfer,
     compute, swapchain), which puts `Context*` in the registry context.
  2. Creates `NUM_FRAMES` (3) per-frame fences (signalled) and the transfer and compute semaphores.
  3. Emplaces the `ModelManager` and `RenderSettings` context variables. `RenderSettings` gets the graphics level
     first (`applyGraphicsPreset` of `Graphics::presets[Graphics::current]` when a `Graphics` context variable
     with a level exists), then the command line: `lighting=off|low|medium|high` sets the tier of both targets
     (`RenderSettings::Lighting::of`), `probe_rays=<n>` (>= 0) `probe_rays_per_frame`, `far_plants=volumes|rays|screen|<other>`
     `far_plants_in_rays` (volumes, rays) and `far_plants_on_screen` (volumes, screen).
  4. Makes the shared state, in order: `Lights`, with ray tracing (`ctx.device.ray.enabled()`) `RayScene`,
     `ProbeVolume` and `PlantVolumes`, then [TreeShade](service-TreeShade.md), [SoilMaps](service-SoilMaps.md),
     [ClassicRender](service-ClassicRender.md), [VRRender](service-VRRender.md) (which makes [VRActions](service-VRActions.md)).
  5. Makes the passes: `SkyPass` always first, then the scene's `renderer.passes` in the order listed
     (`terrain`, `grass`, `vegetation`, `animals`, `model`, `ui`); an unknown name logs `Renderer : unknown pass <name>`
     and is skipped. Each pass gets one pipeline per target it `supports()`: the scene render pass of the target
     (`getRenderpass()`, the one of `SunVisibility` with ray tracing) or, for an `overlay()` pass, the final one
     (`getFinalRenderpass()`); the VR pipeline only when VR is enabled.
- **preinit()**: nothing.
- **init()** (after the systems' and the worlds' `init`): flushes the transfer queue (uploads of textures, fonts
  and models made during init), waits for the device to be idle and checks the transfer.
- **prepare()** (start of every frame, before `pre_tick`): advances `ctx.frame_index` modulo `NUM_FRAMES`, waits
  for that frame's fence (profiler zone "Wait for the frame's fence"), then `ClassicRender::prepare` (acquires the
  window's image unless the window is hidden) and `VRRender::prepare` (OpenXR events, `xrWaitFrame` / `xrBeginFrame`,
  controller sync).
- **render()** (after `post_tick`):
  1. Time of the frame: seconds since the renderer was made, or `FrameClock::time` when the run is deterministic
     (`RunArgs::deterministic()`, captures and probes).
  2. `transfer.check()`, `gpu_profile.beginFrame()`.
  3. Updates on the main thread, in order: `Lights::update`, `TreeShade::update`, `SoilMaps::update`, every pass'
     `update(frame_index)` in draw order, then with ray tracing `PlantVolumes::prepare`, `RayScene::update`,
     `ProbeVolume::update(time)`, `PlantVolumes::update`.
  4. Submits the transfer commands (if any; signals the transfer semaphore) and the compute commands (if any;
     waits on the transfer, signals the compute semaphore). The compute buffer holds what the systems recorded
     during the tick (marching cubes of the `Terrain` system, animal skinning) and the lighting's work.
  5. `ClassicRender::updateView` if the window has an image, `VRRender::acquire` + `updateView` if the headset
     wants a frame.
  6. Records both targets **concurrently**: one taskflow with a task per active target, run on the
     `tf::Executor` context variable and waited for. The passes' `record()` only read shared state.
  7. One graphics submission with both command buffers, waiting on the compute (or transfer) semaphore and the
     window's acquire semaphore, signalling the window's present semaphore and the frame fence.
  8. `VRRender::endFrame()` (releases the eye images, `xrEndFrame`, also when nothing was rendered),
     `ClassicRender::present()`.
- **requestCapture()** / **takeCapture()**: asks `ClassicRender` to copy this frame's swapchain image into a
  host buffer; `takeCapture` waits for the frame fence and returns it as RGB8 (BGRA swapped).
- **finish()**: waits for the device to be idle, clears the `ModelC` storage (GPU buffers held in the registry
  must go before the device) and erases the `ModelManager` context variable.
- **Destructor**: destroys passes, VR, desktop, soil maps, tree shade, plant volumes, probes, ray scene and lights
  in that order, then the per-frame fences and semaphores. The `Context` member goes last.

## Parameters

From the scene file's `renderer` block (`RendererDef`, [`src/scene/scene_def.h`](../../../src/scene/scene_def.h))
and the command line (`RunArgs`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `renderer.passes` | list of strings | `[]` | the scene passes in draw order (`terrain`, `grass`, `vegetation`, `animals`, `model`, `ui`); the sky is always drawn first |
| `renderer.vr` | bool | `true` | `false`: `Game` sets `Settings::vr_mode = 0` before the renderer is made, no headset |
| `graphics=<level>` | string | config (`high`) | level of `resources/graphics.json` applied to `RenderSettings` first (`Graphics` context variable) |
| `lighting=<tier>` | string | empty | `off`, `low`, `medium`, `high`: the lighting tier of both targets |
| `probe_rays=<n>` | int | -1 (keep) | `RenderSettings::probe_rays_per_frame` |
| `far_plants=<mode>` | string | empty (keep) | `volumes`, `rays`, `screen` or anything else (neither) |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| ModelManager | context | write+create | constructor | the glTF cache the systems load models through |
| ModelManager | context | destroy | finish | erased before the device goes |
| RenderSettings | context | write+create | constructor | emplaced, then the graphics level and the command line's lighting, probe rays and far plants written in |
| Graphics | context | read | constructor | `current` and `presets`: the level to apply |
| RunArgs | context | read | constructor, render | `lighting`, `probe_rays`, `far_plants`; `deterministic()` for the time of the frame |
| FrameClock | context | read | render | `time` of the frame in deterministic runs |
| Executor | context | read | render | the `tf::Executor` the two targets are recorded on |
| ModelC | component | destroy | finish | `reg.clear<ModelC>()`: the meshes' GPU buffers go before the device |

What the objects it owns touch is on their own pages: [Context](service-Context.md), [ClassicRender](service-ClassicRender.md),
[VRRender](service-VRRender.md), [VRActions](service-VRActions.md), [TreeShade](service-TreeShade.md),
[SoilMaps](service-SoilMaps.md), [ModelManager](service-ModelManager.md); the lighting objects (`Lights`,
`RayScene`, `ProbeVolume`, `PlantVolumes`, `SunVisibility`) on [service-Lights](service-Lights.md),
[service-RayScene](service-RayScene.md), [service-ProbeVolume](service-ProbeVolume.md),
[service-PlantVolumes](service-PlantVolumes.md), [service-SunVisibility](service-SunVisibility.md); the passes on
[pass-sky](pass-sky.md), [pass-terrain](pass-terrain.md), [pass-grass](pass-grass.md),
[pass-vegetation](pass-vegetation.md), [pass-animals](pass-animals.md), [pass-model](pass-model.md), [pass-ui](pass-ui.md).

## Side effects

- Creates the SDL window, the OpenXR session, the Vulkan device and swapchain (through `Context`).
- GPU: per-frame fences and semaphores; one transfer, one compute and one graphics submission per frame; waits
  on the frame fence (CPU blocks when the GPU is behind) and on device idle in `init` and `finish`.
- Threads: records the desktop and headset command buffers in parallel on the taskflow executor.
- Log: `Renderer : unknown pass <name>` (error).
- Profiler: zones `create renderer` children (`create lighting and targets`, `create pass <name>`,
  `create pipelines <name>`), `update <pass>` per pass, `Update`, `Record`, `Submit`.
- Window: presents the desktop image; OpenXR `xrEndFrame` every frame a headset frame was begun.

## Order and dependencies

- Made before the `SystemSet`: systems' constructors and `init` may use `Context*`, `ModelManager`,
  `RenderSettings`, `VRInput`, `VRRig`, `SoilLayers` and `Sky` (emplaced by `Lights` if absent).
- `init()` runs after the systems' `init`, so their startup uploads are flushed before the first frame.
- `prepare()` before `pre_tick`: the frame fence is waited for before systems record compute work into the
  frame's command buffer (the `Terrain` system) or write per-frame GPU buffers.
- `render()` after `post_tick`: the passes' `update` see this frame's components (`Transform`s interpolated by
  `Physics`, `Camera` written by the view systems).
- `finish()` after the systems' `finish`; the `SystemSet` (GPU resources of `Terrain`) is destroyed before the
  renderer in `Game::~Game`.

## Used in scenes
<!-- generated -->

## Tests

- Visual tests (need a GPU, label `gpu`, `CMakeLists.txt`, [`tools/visual_test.py`](../../../tools/visual_test.py)):
  `V0.1` / `V0.2` (base captures deterministic / match the goldens), `V1.1`, `V2.1`, `V2.2`, `P1 check`, `P2 check`,
  with the shot scripts in [`tests/capture/`](../../../tests/capture/).
- `GRAPHICS.*` ([`tests/unit/graphics.cpp`](../../../tests/unit/graphics.cpp)): the levels `applyGraphicsPreset` applies.
