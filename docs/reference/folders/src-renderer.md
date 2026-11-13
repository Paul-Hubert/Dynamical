# src/renderer

The Vulkan renderer of the viewer (`dynamical`; `dynamical_train` and `dynamical_sim` have none): the
[Renderer](../systems/service-Renderer.md) that owns the context, the two render targets drawn every frame
(desktop window and headset, recorded in parallel), the shared GPU state of the frame and the scene passes.
Only the viewer links it; `dynamical_sim` must not name a `renderer/` header (test `T1.1`).

## Files

| File | What |
|---|---|
| [`renderer.h`](../../../src/renderer/renderer.h) / [`renderer.cpp`](../../../src/renderer/renderer.cpp) | `Renderer`: makes context, lighting, tree shade, soil maps, targets and passes; the frame (prepare, updates, transfer → compute → graphics submissions, parallel recording, present); captures |
| [`scene_pass.h`](../../../src/renderer/scene_pass.h) | `RenderTarget` (Desktop, VR), `RenderView` (what a pass records for: target, frame, eye, view set, extent), the `ScenePass` interface (`createPipeline`, `update`, `record`, `supports`, `overlay`), `PassZones` (profiler zone names) |
| [`view_ubo.h`](../../../src/renderer/view_ubo.h) | `ViewUBO`: set 0 binding 0 of every scene pipeline (view projection, position + time, tree shade, inverse, previous view projection and position, lighting tier) |
| [`tree_shade.h`](../../../src/renderer/tree_shade.h) / [`tree_shade.cpp`](../../../src/renderer/tree_shade.cpp) | `TreeShade`: crown shadows of the plants on a 2048 m wrapping map (binding 1); `treeShadeView()` |

Subfolders: [context](src-renderer-context.md), [classic_render](src-renderer-classic_render.md),
[vr_render](src-renderer-vr_render.md), [model](src-renderer-model.md), [util](src-renderer-util.md),
[terrain](src-renderer-terrain.md), [shaders](src-renderer-shaders.md), `passes` and `lighting` (their own pages:
[src-renderer-passes](src-renderer-passes.md), [src-renderer-lighting](src-renderer-lighting.md)).

## Systems

None registered here. Objects that touch the registry: [Renderer](../systems/service-Renderer.md),
[TreeShade](../systems/service-TreeShade.md).

## Components and context variables

None defined in this folder (`ViewUBO` is a GPU buffer layout, not in the registry).

## Worth knowing

- `Renderer::prepare()` / `render()`: the frame (see the page of the service).
- `ScenePass`: `update()` on the main thread, `record()` concurrently for both targets and only reading
  shared state (`const entt::registry&`).
- `treeShadeView(reg)`: `ViewUBO::tree_shade` from `RenderSettings`.

## Dependencies

- `src/logic/components` (`RenderSettings`, `Camera`, `Transform`, `FrameClock`, `VRRig`, `VRInput`),
  `src/logic/settings` (`Settings`, `Graphics`, `applyGraphicsPreset`), `src/logic/sky` (`Sky`, `skyLight`),
  `src/logic/vegetation` (`Plant`, `VegetationLibrary`), `src/scene` (`RendererDef`, `RunArgs`),
  `src/view/components` (`Input`), `src/util` (log, profile), taskflow, EnTT, Vulkan, OpenXR, SDL2.
- Made and driven by `src/view/game.cpp`.

## Design docs

[docs/lighting.md](../../lighting.md), [docs/graphics.md](../../graphics.md), [docs/profiling.md](../../profiling.md),
[docs/forest.md](../../forest.md) (tree shade), [docs/port-mc-grass-gltf-vr-jolt.md](../../port-mc-grass-gltf-vr-jolt.md).
