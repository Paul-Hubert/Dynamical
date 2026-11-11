# Grass

Grass has no blade geometry. `GrassPass` draws the terrain chunks a second time as a shell at the top of the grass, and each pixel finds its blade in rays baked once against a 3D model of grass. This is the Unity `grass45` shader (`Assets/Terrain/grass`, `Texture45Generator.cs` and `grassShader45.shader`) rebuilt for this engine; it replaces the 2019 lookup of 2D rays against circles.

![Grass at eye level](images/grass/eye.png)

![From the top](images/grass/top.png)

The lookup itself is described here. What makes it look less like a tile (layers of several sizes and heights, patches, blade colours, curved blades, shadows) was added in ten steps, each with its design and what came out of it: [grass/plan.md](grass/plan.md). The rays were baked at 45° only at first; the other elevations and the anisotropic filtering are [grass/12-elevations.md](grass/12-elevations.md).

## Files

| File | Role |
|---|---|
| `src/logic/grass/grass_model.{h,cpp}` | Generates the tile of grass blades (`generateGrassTile`), exports it as OBJ |
| `src/logic/grass/grass_bake.{h,cpp}` | Ray tracer over the repeated tile (`GrassTracer`), baker (`bakeGrass`), file, mips, CPU mirror of the shader lookup (`lookupGrass`) |
| `src/logic/grass/grass_textures.{h,cpp}` | The rays as the GPU gets them: heights apart from normals and ids, mips, BC4 / BC5 blocks (`makeGrassTextures`), tests `GRASS.8` to `GRASS.10` in `tests/unit/grass_textures.cpp` |
| `src/renderer/passes/grass_pass.cpp` | Loads or bakes the rays, uploads the textures, draws the shells |
| `src/renderer/shaders/grass.{vert,frag}.glsl` | The shell and the lookup |
| `tools/raycast/main.cpp` | Command line baker, OBJ export, comparison with ray tracing |
| `tests/unit/grass.cpp` | `GRASS.1` to `GRASS.10` |
| `tests/capture/grass.capture.json` | Fixed cameras over the same grass, to compare captures |

The baker is part of `dynamical_sim` (no Vulkan), so the viewer, the tool and the tests share it. `dynamical` bakes `resources/grass_raycast.bin` in its run directory the first time (3.2 s in RelWithDebInfo, not measured in Debug; a 67 MB file) and again when the file's key no longer matches `GrassBakeParams` or the baker version. Nothing baked is committed.

## The grass model

`generateGrassTile` builds one tile, 1 × 1 on the ground: 9 × 9 tufts on a jittered grid, 4 to 7 blades per tuft that fan out and bend away from its center (463 blades, 8334 triangles). A blade is a strip folded in a V along its length, with smooth normals and a random id. The tallest tip is at 0.98; the seed fixes everything.

`GrassTracer` repeats the tile to infinity: its acceleration grid covers one tile and each cell lists the triangles of whichever copy reaches it, so a ray walks from tile to tile until it hits a blade or the ground plane. Every baked ray therefore hits something.

## What is baked

Tile space: x and z on the ground in tiles, y from 0 at the roots to 1 at the top of the grass.

- **Oblique rays** (2D array, 256 × 256 × 256 layers): rays going down at 22.5°, 45° and 67.5° under the horizon, for 64, 64 and 32 azimuths, and before them the lowest rays, for 96 azimuths ([grass/12-elevations.md](grass/12-elevations.md)). Texel (x, z) of an azimuth's layer is the ray that crosses **the middle of the grass** (y = 0.5) above (x, z): it starts at y = 1 half its reach before (the tiles it crosses: 2.41, 1, 0.41) and reaches the ground as far after.
- **The lowest rays** answer for the views along the ground: from the top of the grass to its middle over 3 tiles (9.5°), above their texel half way. A horizontal ray would meet nothing at the top of the grass, and only streaks inside it.
- **Top rays** (2D texture, 256 × 256): rays straight down.

Each texel is RGBA8: r the height of the hit (0 ground, 1 top; for the lowest rays, the part of their way that was left), g b its normal, facing the ray, in octahedral coordinates, a the id of the blade (0.5 on the ground). Texels are baked at their centers, the textures repeat and have mips: 90 MB on the GPU. They are sampled trilinear with 16 times anisotropy, over the footprint of the pixel on the ground; the shader blends the two nearest azimuths itself (GPUs do not filter 3D images anisotropically, hence the array).

## The lookup

For a pixel of the shell and one layer, in `grass.frag.glsl` (and `lookupGrass` on the CPU):

1. **Tile space ray.** The blades grow along `base_normal`, lean with the terrain normal (`follow_terrain`) and with the wind. Leaning shears tile space around the ground: a point at height y is `y × lean` away from its root, so roots do not move whatever the wind and the normal do. The view ray becomes a straight ray in tile space, with an azimuth and a `reach` (tiles crossed while going down the whole grass: 1 at 45°, 0 from the top).
2. **Middle of the grass.** The ray is looked up where it crosses y = 0.5, in the oblique rays of its azimuth, at the two baked elevations around its own. Those baked rays go through the same point; they part from the view ray by the same amount at the top and at the bottom instead of drifting all the way down from a shared entry point.
3. **Carry the hit to the view ray.** A baked ray met a blade `(0.5 - r) × its reach` tiles after the middle. Blades are mostly vertical, so the view ray meets the same blade over the same place, higher or lower: `height = 0.5 - (0.5 - r) × baked reach / reach`. At a baked elevation this is the baked hit itself.
4. **Between two elevations.** The two heights, normals and ids are blended, linearly in the reach of the ray. Over 67.5° the second view is the top down texel of the same point (`top_start`); the lowest rays answer alone for everything under them.
5. **Layers.** The grass is `layers` tall layers (2) and an undergrowth: the same tile turned by the golden angle, shifted, at its own tile size and height, bent by its own noise. Each is looked up as above; the closest hit on the ray wins.
6. **Depth and light.** The hit gives the distance along the view ray, written to `gl_FragDepth` (`depth_greater`: never in front of the shell). The normal goes back to world space through the sheared tile axes. Blades are darker under the canopy, shadowed by their neighbours, let some light through, and have their own colour.

The terrain pass knows the grass is there: under it the terrain is dark and its depth is pushed back by the grass height, because the grass pass only knows the ground as a plane under each pixel and its hits can end up a little under the real terrain ([grass/03-patches.md](grass/03-patches.md)).

Rays closer to the horizontal than a reach of 3 are looked up as if they had it, which keeps the hits of hill crests close: this is what the lowest rays are baked for. The footprint of the pixel in the textures comes from its size on the ground, across and along the view, not from the lookup coordinates (they jump between blades and at the azimuth wrap).

The shell is `height × smoothstep(0.68, 0.86, normal.y)` above the terrain, per vertex, and cut where the pixel's normal is under 0.74: grass gets shorter towards the rock and stops with it.

How tall the grass is also changes across the world: octaves of low frequency Perlin noise, baked for each chunk in a 65 × 65 map on its terrain vertices (`src/logic/grass/grass_height.{h,cpp}`, a layer of a 2D array per chunk) that the vertex shader reads, from 0.55 m to 1 m with the defaults ([grass/11-height-map.md](grass/11-height-map.md), `GRASS.8`).

## How close to ray tracing

`raycast --preview out.png` renders the tile ray traced and looked up, side by side, and prints the mean error of the hit height, in grass heights, over 40 000 rays from every side (one layer, 256 texels, 96 + 64 + 64 + 32 azimuths). Under 18.4° the hit is where the blade is met on the ground, as a height on the ray that is looked up:

| Elevation | 5° | 10° | 15° | 22.5° | 30° | 45° | 60° | 67.5° | 75° | 90° |
|---|---|---|---|---|---|---|---|---|---|---|
| Lookup | 0.17 | 0.14 | 0.14 | 0.11 | 0.11 | 0.10 | 0.13 | 0.10 | 0.13 | 0.03 |
| With the 45° rays alone (before step 12) | 0.42 | 0.27 | 0.13 | 0.11 | 0.12 | 0.10 | 0.16 | | 0.17 | 0.03 |
| Constant height (the mean) | 0.05 | 0.15 | 0.13 | 0.12 | 0.13 | 0.16 | 0.17 | 0.18 | 0.17 | 0.17 |

The baked elevations are at the floor of the textures: their error is the filtering (blades are 6 texels wide at the root) and the interpolation between two azimuths. Between them the error rises by a third at most. Under about 15° the lookup is no better than a flat surface in depth, it only carries the look of the blades: most traced rays go further than the 3 tiles a lookup can tell.

Anchoring matters: with the same rule but the rays looked up at their entry point at the top of the grass, the error at 30° was 0.22 instead of 0.11 on 128 texels (`GRASS.4` checks this without textures, at 25°, 35°, 55° and 65°).

## Settings

`RenderSettings::Grass`, live in the debug menu (Grass): tile size (1 m), height (1 m, of the tallest grass), the height map (wavelength, octaves, gain, contrast, amount), base normal, follow terrain, wind strength and speed, top blend start, noise frequency and amplitude, and those of the steps of [grass/plan.md](grass/plan.md): layers, layer spread, undergrowth, patch size and amount, layer patches, curve, colour variation, dryness, shadow, canopy, silhouette, azimuth dither. Texture size, the azimuths of each elevation, the lowest rays and the model are `GrassBakeParams` / `GrassModelParams` defaults; changing them changes the key and the next run bakes again.

```bash
cmake --preset vcpkg -DDYNAMICAL_BUILD_TOOLS=ON
cmake --build build --config RelWithDebInfo --target raycast
build/RelWithDebInfo/raycast out.bin --obj tile.obj --preview preview.png   # [--size 256] [--angles 96,64,64,32] [--low 1,0.5] [--seed 7]
```

## Textures and cost

On the GPU the oblique rays and the top rays are three textures each (`makeGrassTextures`): the height of the hit, its normal, the id of its blade. The layers of the grass read heights only; the normal and the id are read once, for the closest hit, from the baked view that weighs the most in its height (`hitTexel`). Heights and ids are BC4 blocks, normals BC5 (R8 and RG8 where the GPU has no BC textures): 45 MB instead of 89 MB of RGBA8. The blocks are made when the pass starts, from the same file (0.3 s on every core). `raycast --preview out.png --blocks` reads the rays back from the blocks: the error of the hit height does not move (0.103 at 45° either way).

`dynamical gpu_times=true` writes a GPU timestamp before the first scene pass of the desktop target and after each one, and logs their means at every capture and every 300 frames; the window then presents without waiting for the monitor, and a capture keeps the window size that was asked for. `tests/capture/grass_times.capture.json` holds each camera of `grass.capture.json` for 300 frames:

```bash
cd build && RelWithDebInfo/dynamical capture=../tests/capture/grass_times.capture.json gpu_times=true window_width=3840 window_height=2160
```

Grass pass on an RTX 5080 at 3840 × 2160, milliseconds, one run of 300 frames per view, scene `base`, default settings:

| View | One RGBA8 texture | Heights apart, normal and id blended like the heights | | Normal and id from one view (what is in) | |
|---|---|---|---|---|---|
| `grass_eye` | 1.54 | 1.55 | +1 % | 1.39 | -9 % |
| `grass_low` | 0.69 | 0.72 | +4 % | 0.62 | -10 % |
| `grass_45` | 1.45 | 1.51 | +4 % | 1.39 | -5 % |
| `grass_top` | 1.36 | 1.50 | +10 % | 1.38 | +1 % |
| `grass_far` | 1.43 | 1.40 | -2 % | 1.22 | -15 % |
| `grass_skim` | 0.48 | 0.51 | +5 % | 0.45 | -7 % |

Blended from the same views as the heights, the split halves the memory and saves no time: the normal and the id of the closest hit are up to 8 anisotropic fetches of their own (two azimuths of two elevations, two textures), which costs what the lighter heights save. Captures are 0.1 to 1.6 / 255 away from those of the RGBA8 texture on average (0.01 to 0.46 % of the pixels more than 24 / 255 away). On the single 3D texture of 64 azimuths at 45° this grass had before, the same change took 7 to 30 % off the pass (0.89 to 0.71 ms for `grass_eye`).

So the normal and the id come from the baked view that weighs the most, instead of a blend of up to four: 2 fetches, 5 to 15 % off the pass in five of the six views. It is another look: 3 to 6 % of the pixels of the close views are more than 24 / 255 away from the blend (a blade keeps the colour of one view, dry blades stand out more, a little more grain). `build/grass_compare` had both side by side when this was chosen.

## Limits

- Seen from inside the grass (camera under the shell), the shell is culled and the terrain shows around the camera.
- Views between two baked elevations blend two views, and so do views between two azimuths (3.75° to 11° apart): thin blades ghost slightly when the texture is magnified (half removed by `azimuth_dither`, at the price of some grain).
- Views along the ground all look like the lowest rays: 9.5° under the horizon.
- The ground between the blades is drawn 12 % of the grass height above the terrain, and the terrain steps back under the grass: something buried less than the grass height under flat ground shows through it.
- Shadows are between blades of one layer only: nothing else casts a shadow on the grass, and the grass casts none.
- Where the ground falls away under the ray (crests, edges of a step) what is behind shows between the blades: the ray comes out of the grass again, and what the lookup met deeper than it ever gets is not drawn ([grass/13-see-through.md](grass/13-see-through.md)). Far away, where blades are not told apart, that is a line a little under the top of the grass.
- About 25 to 35 texture fetches per pixel with 3 layers, up to 15 of them anisotropic. The frame time was not measured, and VR was not tried.
