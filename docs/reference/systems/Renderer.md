# Renderer

> **Kind:** service · **Registry name:** — · **Scope:** n/a (host registry) · **Lane:** per-frame call from `Game::start` (`prepare()` before `pre_tick`, `render()` after `post_tick`) · **Needs renderer:** yes
> **Source:** [`src/renderer/renderer.h`](../../../src/renderer/renderer.h), [`src/renderer/renderer.cpp`](../../../src/renderer/renderer.cpp)

## Purpose
Top-level owner of rendering: the Vulkan/OpenXR `Context`, the desktop target (`ClassicRender`), the headset target (`VRRender`) and the scene passes listed by the scene's `renderer` block. Each frame it waits for the frame-in-flight fence, lets both targets prepare, then after the systems ticked runs pass updates, submits transfer -> compute -> graphics chained by semaphores, records both targets concurrently on taskflow and submits them in one graphics submission. Also provides frame capture for `Capture` (`requestCapture` / `takeCapture`).

## Lifecycle
- **Construction / params:** `Renderer(entt::registry& reg, const RendererDef& desc)`; `desc` comes from the scene JSON `"renderer": { "passes": [...], "vr": bool }` (scene loader validates: `passes` required array of distinct names from `terrain`, `grass`, `model`, `ui`; `vr` optional bool, default `true`; `Game` sets `Settings::vr_mode = 0` when `vr` is false). In order:
  1. Builds `Context` (VRContext -> Windu -> Instance -> Device -> Transfer -> Compute -> Swapchain), which emplaces `Context*`, then runs `VRContext::init` and creates `view_layout`.
  2. Creates `NUM_FRAMES` (3) per-frame fence (signaled), transfer semaphore and compute semaphore.
  3. Creates `ClassicRender` and `VRRender` (which emplaces `VRRig` and, via `VRActions`, `VRInput`).
  4. Emplaces `ModelManager` and a default `RenderSettings`.
  5. Creates the passes in list order (`terrain` -> TerrainPass, `grass` -> GrassPass, `model` -> ModelPass, `ui` -> UIPass; an unknown name logs `Renderer : unknown pass` and is skipped). Builds the Desktop pipeline of every pass supporting it and, if VR is enabled, the VR pipeline.
  6. Records `start_time`.
- **preinit:** empty.
- **init:** called after the systems' `init`. Flushes the transfer command buffer (startup uploads: textures, fonts, models), `waitIdle`, `transfer.check()` (marks uploads ready).
- **prepare()** (per frame, before systems tick):
  1. `frame_index = (frame_index + 1) % 3`.
  2. Waits on that frame's fence (unbounded).
  3. `ClassicRender::prepare` (acquire swapchain image unless the window is hidden), `VRRender::prepare` (OpenXR events, `xrWaitFrame`, `xrBeginFrame`, action sync).
- **render()** (per frame, after systems):
  1. `time` = seconds since construction (steady clock), or `FrameClock::time` when `RunArgs` exists and `deterministic()`.
  2. `transfer.check()`; `update(frame_index)` of every pass on the main thread.
  3. If the transfer batch is non-empty: flush it, signaling the transfer semaphore. If the compute batch is non-empty: flush it waiting on the previous semaphore (if any), signaling the compute semaphore. The last signaled semaphore becomes a graphics wait (`eAllCommands`).
  4. `do_classic = ClassicRender::active()`, `do_vr = VRRender::active()`. Writes the desktop ViewUBO; for VR acquires eye images and writes per-eye ViewUBOs.
  5. Builds a taskflow with one task per active target (`record(frame_index, passes)`) and waits for it on the `tf::Executor` context variable.
  6. Collects command buffers; desktop adds the acquire semaphore wait (`eColorAttachmentOutput`) and present semaphore signal. Resets the fence and submits once to the graphics queue (also when no target is active, with whatever semaphores were collected).
  7. `VRRender::endFrame()` (release images, `xrEndFrame`), `ClassicRender::present()`.
- **finish:** `waitIdle`, `reg.clear<ModelC>()`, `reg.ctx().erase<ModelManager>()`. Destructor: destroys passes, VR then desktop targets, the per-frame sync objects, then the `Context` members (reverse declaration order).

## Expected behavior
- At most 3 frames in flight; `prepare` blocks on the fence of the frame 3 frames ago.
- A mesh recorded by `TerrainSys` in this frame's compute batch and uploads recorded this frame are visible to this frame's graphics work.
- Desktop and VR are independent: a hidden window skips the desktop target, a stopped/unfocused/not-rendering session skips VR, and the other target still renders.
- With `RunArgs::deterministic()` the shader time (`ViewUBO::position.w`) is `FrameClock::time`, so captures are reproducible.
- `requestCapture` returns false when the swapchain is not transfer-source capable or no image was acquired; `takeCapture` waits for the current frame fence and returns RGB8 pixels (false if empty).
- Construction throws if no Vulkan device is suitable or a required resource file is missing (pass constructors).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Settings`](../components/Settings.md) | host | read | construction | `vr_mode` (VRContext), `window_width`, `window_height`, `fullscreen` (Windu) |
| [`RunArgs`](../components/RunArgs.md) | host | read | construction | `window_hidden` (Windu, `find`) |
| [`Context*`](../components/Context.md) | host | write +emplace_ctx | construction | Pointer to the member `Context` |
| [`ModelManager`](../components/ModelManager.md) | host | write +emplace_ctx | construction | |
| [`RenderSettings`](../components/RenderSettings.md) | host | write +emplace_ctx | construction | Defaults |
| [`RunArgs`](../components/RunArgs.md) | host | read | render | `deterministic()` (`find`) |
| [`FrameClock`](../components/FrameClock.md) | host | read | render | `time` when deterministic |
| [`tf::Executor`](../components/tf-Executor.md) | host | read | render | Parallel recording |
| [`ModelC`](../components/ModelC.md) | host | write +remove | finish | `reg.clear<ModelC>()` |
| [`ModelManager`](../components/ModelManager.md) | host | write +remove | finish | `ctx().erase` |

Accesses made by the owned objects are documented separately: [ClassicRender](ClassicRender.md), [VRRender](VRRender.md), [VRActions](VRActions.md) and the passes.

## Side effects
- Creates the SDL window, OpenXR instance/session, Vulkan instance, device, swapchain (through `Context`).
- Queue submissions (transfer, compute, graphics), presentation, `xrEndFrame`.
- Spawns 1-2 taskflow tasks per frame for command recording and blocks until they finish.
- Blocks on fences in `prepare`, `takeCapture`, and on `waitIdle` in `init`/`finish`.
- Logs unknown pass names.

## Ordering and dependencies
- Constructed before the `SystemSet` and the worlds (systems' `preinit`/`init` can use `Context*`, `ModelManager`, `RenderSettings`, `VRRig`, `VRInput`); `init` runs after the systems' and worlds' `init`.
- Frame: `prepare` -> systems `pre_tick` -> `post_tick` -> `render`. Every write that should appear in this frame (camera, `VRRig`, transforms, compute work) must happen in the systems.
- `VRInput` is refreshed in `prepare`, so systems in the same frame see the current controller state.
- During recording, only read-only registry access is allowed (passes use `const entt::registry&`); pass `update` is the place for main-thread mutation.
- `Game::~Game` calls systems' `finish`, then `Renderer::finish`, then destroys the systems (TerrainSys frees GPU resources), then the Renderer.

## Related
[ClassicRender](ClassicRender.md), [VRRender](VRRender.md), [VRActions](VRActions.md), [TerrainSys](TerrainSys.md), [TerrainPass](TerrainPass.md), [GrassPass](GrassPass.md), [ModelPass](ModelPass.md), [UIPass](UIPass.md), [ModelManager](ModelManager.md), [`src/renderer/`](../folders/src-renderer.md), [`src/renderer/context/`](../folders/src-renderer-context.md).
