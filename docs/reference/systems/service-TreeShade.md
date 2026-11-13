# TreeShade

`TreeShade` in [`src/renderer/tree_shade.cpp`](../../../src/renderer/tree_shade.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by [Renderer](service-Renderer.md), always there) · Parameters: no (`RenderSettings::tree_shade`)

The shade of the plants on the ground without shadow maps: every `Plant` taller than 1.5 m draws the shadow of
its crown as two soft discs on a 2048 x 2048 R8 map of the world seen from above (a texel a metre, wrapping every
2048 m), moved to where the light of the `Sky` (the sun or the moon) puts them. Binding 1 of the view set: the
terrain, grass, vegetation and animal fragment shaders read it (`treeShade()` in
[`shaders/include/tree_shade.glsl`](../../../src/renderer/shaders/include/tree_shade.glsl)), and with ray tracing
the probes start from it. Plants are followed through the registry's signals; they are not expected to move.

## Behaviour

- **Constructor**: makes the sampler (linear, repeat; `ctx.tree_shade_sampler`), takes the light direction of
  `skyLight(Sky)` when there is a `Sky` (straight up otherwise) and its ground slope
  (`light.xz / max(light.y, 0.2)`); makes sure the `Transform` storage exists; draws every existing `Plant`;
  connects `on_construct<Plant>` and `on_destroy<Plant>`; uploads the first map.
- **onPlant** (signal handler, and for every plant at construction): needs the `VegetationLibrary` context
  variable, a `Transform` on the entity and a valid species; `tall = species.height x scale.y`, ignored under
  1.5 m; crown radius `min(species.radius x scale.y, 0.6 tall + 1)`, strength `0.9 clamp((tall - 1.5) / 4, 0.15, 1)`.
  Two discs (middle of the crown at half the height, radius 0.85 r; top at 0.8 of the height, 0.6 r) are added to
  the optical depth map and their 64 x 64 tiles marked dirty. A plant whose `Transform` comes after its `Plant`
  is not drawn.
- **onPlantDestroyed** (signal handler): subtracts its discs again.
- **update()** (`Renderer::render`, main thread, before the transfer submission): destroys image views retired
  more than `NUM_FRAMES + 1` frames ago; `followLight`: when the light of the `Sky` moved more than 1° (10° with
  ray tracing) since the shadows were drawn, clears the map and draws every crown again; if anything is dirty,
  converts the dirty tiles to bytes (depth x 255 / 4) and uploads a **new** image through the transfer queue
  (`ctx.tree_shade_view` points at it; the old one is retired, the view set is rewritten every frame by
  `Context::writeViewSet`).
- **treeShadeView(reg)** (free function, called by both targets' `updateView`): `ViewUBO::tree_shade` =
  (`RenderSettings::tree_shade`, 1 / 2048, 700 m where the shaders stop reading it, 0); strength 0 without
  `RenderSettings`.
- **Destructor**: disconnects the signals, waits for the device, destroys the views and the sampler.

## Parameters

None of its own. `RenderSettings::tree_shade` is the strength the shaders apply (0 turns it off).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| Plant | component | read | constructor, `on_construct` / `on_destroy` handlers | which plants cast a shade, their species |
| Transform | component | read | constructor (storage), `onPlant` | root position and scale of a plant |
| Sky | context | read | constructor, update (`followLight`) | direction of the light that casts the shadows |
| VegetationLibrary | context | read | `onPlant` | height and radius of the species |
| RenderSettings | context | read | `treeShadeView` | `tree_shade`, the strength put in the `ViewUBO` |

## Side effects

- Signals connected: `on_construct<Plant>`, `on_destroy<Plant>` (disconnected in the destructor).
- GPU: a 4 MB R8 image uploaded through `Transfer` every frame a plant came or went or the light moved; the old
  images are kept for the frames in flight; sets `Context::tree_shade_view` / `tree_shade_sampler`.
- CPU: 16 MB of floats and 4 MB of bytes for the map.

## Order and dependencies

- Made after `Lights` (which emplaces `Sky` if absent). Plants made by `Forest`, `VegetationScatter`,
  `VegetationLineup` are seen through the signal; their `Transform` must be emplaced before their `Plant`.
- `update` runs before the targets' `updateView`, which rewrite binding 1 with the current view.

## Used in scenes
<!-- generated -->

## Tests

- `FOREST.*` ([`tests/unit/forest.cpp`](../../../tests/unit/forest.cpp)) cover the plants it follows, not the map;
  the map is in the forest captures and the lighting shots (`tests/capture/lighting*.capture.json`).
