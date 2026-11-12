# Graphics levels

`resources/graphics.json` holds five levels, from the cheapest: `ultralow`, `low`, `medium`, `high`, `ultrahigh`.
Each one gives a value to everything the time of a frame and the memory of the GPU depend on and that the engine
can change without being built again.

## Picking one

- `graphics=<name>` on the command line of `dynamical`, or `"graphics"` in `config.7.json` (`high` when the file
  is made).
- The debug menu has a `Graphics` list: it applies a level while the engine runs, except what is read at the
  start (see the table).
- What the command line asks for itself stays over the level: `view_distance=`, `ray_tracing=`, `lighting=`,
  `probe_rays=`, `far_plants=`.
- Captures and probes (a fixed clock) are drawn with the defaults of the code unless `graphics=` is given: the
  goldens do not depend on the config file. `high` repeats those defaults (test `GRAPHICS.2`): change a default in
  `RenderSettings` and `high` with it.
- A value a level leaves out is the default of the code. A file that is missing or wrong, or a name the file
  does not have, is a warning and the defaults.

## What a level holds

| JSON | Where it goes | When | What it costs |
| --- | --- | --- | --- |
| `view_distance` | `Settings::view_distance` | any time | chunks loaded, meshed and drawn, plants and animals on them (grows with its square) |
| `ray_tracing` | `RunArgs::ray_tracing` | start | false: no structures, probes, sun rays or plant volumes at all, the fallback lighting |
| `lighting.desktop`, `lighting.vr` | `RenderSettings::lighting[2]` | any time | `traced_range` (m the sun is traced per pixel within), `sun_rays` a pixel, `plant_geometry_range` (m plants are their leaves in the rays within), `shadowed_torches` a pixel, `animals` with a shadow |
| `lighting.probe_rays_per_frame` | `RenderSettings::probe_rays_per_frame` | any time | a million is 20 ms in a forest on an RTX 5080 |
| `lighting.rays_per_probe` | `RenderSettings::rays_per_probe` | any time | fewer: more probes a frame for the same budget, noisier light |
| `lighting.far_plants_in_rays` | `RenderSettings::far_plants_in_rays` | any time | far plants as boxes in the rays, read from their baked volume |
| `lighting.far_plants_on_screen` | `RenderSettings::far_plants_on_screen` | any time | far plants drawn from their volume (a march) in place of a billboard |
| `textures.*_anisotropy` | `RenderSettings::textures` | start | samplers of the grass rays, the ground, the plants, the animals (1 to 16) |
| `ground.relief` | `RenderSettings::relief` | any time | 0: flat textures, no lookup in the baked rays of the soil |
| `grass.layers` | `RenderSettings::Grass::layers` | any time | kinds of plants looked up a pixel (1 to 4) |
| `grass.shadow` | `RenderSettings::Grass::shadow` | any time | 0: no second lookup towards the sun for the shadows of the blades |
| `grass.see_through` | `RenderSettings::Grass::silhouette` | any time | 0: the shell is drawn once instead of three times |
| `vegetation.lod_bias` | `RenderSettings::Vegetation::lod_bias` | any time | how far the detailed levels of the plants and the animals are kept |
| `vegetation.normal_maps`, `subsurface` | `RenderSettings::Vegetation` | any time | texture reads and shading of the leaves |
| `streaming.chunk_meshes_per_frame` | `RenderSettings::streaming` | any time | marching cubes dispatches a frame (8 at most, the staging ring) |
| `streaming.ray_chunks_per_frame` | `RenderSettings::streaming` | any time | chunk structures built for the rays a frame |
| `streaming.plant_volume_slices_per_frame` | `RenderSettings::streaming` | any time | slices of a plant volume baked a frame (multiples of 4) |

The streaming budgets do not change what a still frame costs: less of them is a steadier frame while the world
comes in, and a world that comes in later.

## The levels

| | ultralow | low | medium | high | ultrahigh |
| --- | --- | --- | --- | --- | --- |
| view distance (m) | 384 | 512 | 640 | 768 | 1024 |
| ray tracing | no | yes | yes | yes | yes |
| desktop: traced range (m) / sun rays | 0 / 0 | 25 / 1 | 60 / 1 | 150 / 2 | 250 / 4 |
| VR: traced range (m) / sun rays | 0 / 0 | 0 / 0 | 25 / 1 | 60 / 1 | 150 / 2 |
| probe rays a frame / a probe | 0 / 16 | 60 000 / 24 | 120 000 / 32 | 250 000 / 48 | 500 000 / 64 |
| far plants | billboards | boxes in the rays, billboards | volumes | volumes | volumes |
| anisotropy grass / others | 2 / 1 | 4 / 2 | 8 / 4 | 16 / 8 | 16 / 16 |
| relief of the soil | flat | flat | yes | yes | yes |
| grass layers, shadows, see through | 1, no, no | 2, no, no | 3, yes, yes | 4, yes, yes | 4, yes, yes |
| LOD bias, normal maps, subsurface | 0.5, no, no | 1, yes, no | 1.5, yes, yes | 2, yes, yes | 3, yes, yes |

The values of the four levels that are not `high` are a first guess: none of them was measured. `tools/profile_run.py
--name <n> -- graphics=<level> scene=...` (docs/profiling.md) gives the time of each pass for a level.

## Not in the levels

Things that cost and are still constants of the code, or would be new work:

- No render scale, no MSAA: both targets draw at the size of the window or at the size the headset recommends
  (`vr_context.cpp`). A scale for the headset would be the cheapest large lever in VR.
- The sun's rays are traced, filtered and composited at full size (`SunVisibility`), also with the tiers off; only
  `ray_tracing` false takes that away.
- The grass shell and the terrain are drawn on every chunk up to the view distance, without a distance of their own
  or frustum culling; plants are one draw each.
- Memory fixed at the start: probe pages (`ProbeVolume::max_pages`, about 64 MB each), plant volumes
  (`most_texels`), the size of the textures (no mip is skipped), `TreeShade::size`.
- How many plants and animals there are is the scene's (`coverage`, `herd_density`, the forest and herds files).
- `fps_max` is read and saved but nothing limits the frames with it.

## Code

`logic/settings/graphics.{h,cpp}` (`GraphicsPreset`, `loadGraphicsPresets`, `applyGraphicsPreset`, the `Graphics`
context variable), read in `Game`'s constructor, applied to the `RenderSettings` in `Renderer`'s. Tests `GRAPHICS.*`
in `tests/unit/graphics.cpp`.
