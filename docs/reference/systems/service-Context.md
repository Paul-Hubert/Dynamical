# Context

`Context` in [`src/renderer/context/context.cpp`](../../../src/renderer/context/context.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (a member of [Renderer](service-Renderer.md)) · Parameters: no (settings and command line keys)

The Vulkan and OpenXR context of the viewer: in order of construction `VRContext` (OpenXR instance, it picks the
Vulkan extensions and the GPU), `Windu` (SDL2 window and surface), `Instance`, `Device` (queues, VMA,
`RayContext`), `GpuProfile`, `Transfer`, `Compute` and `Swapchain`. It also holds the layout of set 0 of every
scene pipeline (the view set) and the handles its bindings are written from. It puts a pointer to itself in the
registry context so that systems and helpers that only have the registry (the `Terrain` system, `ModelManager`,
`loadGLTF`, the `Input` system) reach the GPU.

## Behaviour

- **Constructor** (first thing the `Renderer` does):
  1. `VRContext`: reads `Settings::vr_mode`; 0 leaves VR disabled. Otherwise enumerates the OpenXR runtime,
     needs `XR_KHR_vulkan_enable`, makes the instance and system; any failure logs `VR disabled : <reason>` and
     the desktop runs alone.
  2. `Windu`: `SDL_Init(video, events)`, a window titled "Dynamical" of `Settings::window_width` x
     `window_height` (1280 x 720 when 0 or less), fullscreen desktop when `Settings::fullscreen`, hidden when
     `RunArgs::window_hidden` (captures and probes still get a surface and a swapchain); starts SDL text input.
  3. `Instance`: Vulkan 1.2 where the loader has it (1.1 otherwise), SDL's and OpenXR's instance extensions, the
     validation layer and debug utils in debug builds.
  4. `Device`: the GPU OpenXR names (or the best scored one), logs `GPU : <name>`, warns when compute subgroup
     arithmetic / ballot are missing (marching cubes need them); `RayContext::detect` with
     `RunArgs::ray_tracing` (default true): ray queries and acceleration structures where the GPU has them,
     logged `Ray tracing : ...`. VMA allocator.
  5. `GpuProfile` (Tracy Vulkan context with `profile=true`), `Transfer`, `Compute`.
  6. `Swapchain`: sRGB BGRA8 surface format, FIFO, or mailbox / immediate when a headset is enabled or
     `RunArgs::gpu_times` (the window must not throttle the loop to the monitor).
  7. `reg.ctx().emplace<Context*>(this)`.
  8. `VRContext::init()`: the OpenXR session, reference space (stage, else local) and a swapchain per eye.
  9. The view set layout: binding 0 `ViewUBO`, 1 tree shade, 2 soil maps, 3 lights; with ray tracing 4 the top
     level structure, 5 the geometry table, 6 up to 512 ray textures, 7 the probe index, 8 / 9 probe pages, 10 to
     12 plant volumes (partially bound arrays).
- **writeViewSet(set, frame, first)**: writes every binding but the `ViewUBO` from the handles the owners keep up
  to date (`tree_shade_view` every time, soil and lights the first time, the structure and the geometry table
  every time, only the new entries of the arrays that grow). Called by the targets when they make their sets and
  every frame in `updateView`.
- **sceneDepthFormat()**: D32 + S8 where the GPU samples it, else D32 / D16 without stencil.
- **Destructor**: destroys the view layout, ends the OpenXR session (`VRContext::finish`); the members go in
  reverse order (swapchain, ... window: `SDL_Quit`).

## Parameters

None of its own: `Settings` (`window_width`, `window_height`, `fullscreen`, `vr_mode`, from `config.7.json` and
`key=value` arguments) and `RunArgs` (`window_hidden`, `ray_tracing`, `gpu_times`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| Context | context | write+create | constructor | `Context*` to itself, for the code that only has the registry |
| Settings | context | read | constructor (`VRContext`, `Windu`) | `vr_mode`; window size and fullscreen |
| RunArgs | context | read | constructor (`Windu`, `Device`, `Swapchain`) | `window_hidden`, `ray_tracing`, `gpu_times` |

## Side effects

- Creates the SDL window (and quits SDL in its destructor), the Vulkan instance, device, queues, VMA allocator,
  the swapchain, the OpenXR instance, session, reference space and eye swapchains.
- Log: `GPU : ...`, `Ray tracing : ...`, `OpenXR runtime ready, VR rendering enabled`, `VR disabled : <reason>`,
  `[OpenXR] ...` debug messages, Vulkan validation messages in debug builds, `Profiler : ...`.
- The `Context*` context variable is never erased: it dangles once the renderer is gone (nothing reads it then).

## Order and dependencies

- Needs `Settings` and `RunArgs` in the context (`Game` emplaces them before the renderer).
- Everything else in the renderer is made after it.

## Used in scenes
<!-- generated -->

## Tests

- Every visual test runs it (see [Renderer](service-Renderer.md)); `T1.1` ([`tests/unit/header_boundary.cpp`](../../../tests/unit/header_boundary.cpp))
  checks that `dynamical_sim` names no Vulkan, SDL, OpenXR or `renderer/` header.
