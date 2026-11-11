# Lighting (design)

Ray traced lighting on the geometry the engine already keeps on the GPU: the chunk meshes, the plants, the
models and the animals become acceleration structures (`VK_KHR_acceleration_structure`), rays are cast with ray
queries (`VK_KHR_ray_query`) from the compute and fragment shaders. The sun, the moon and the sky bounce into
probes baked per chunk and refreshed all the time; shadows near the viewer are traced per pixel, soft, through
leaves that tint the light; torches cast traced shadows and bounce through the same probes. The terrain can be
edited, the lighting follows.

Nothing of this is implemented. This document holds the decisions, the layouts and the order of the work.

## Decisions

| Question | Decision |
|---|---|
| Sun | Moves through a day of 24 minutes by default, with a moon and a night |
| Sky | Procedural, follows the sun; rays that miss read it |
| Storage of the baked light | Irradiance probes every 4 m on a grid per chunk. Not a lightmap: marching cubes has no UVs, an unwrap per chunk plus virtual texturing is the costliest part of the idea for a gain only direct shadows would show |
| How probes follow the sun | Rolling re-bake: a ray budget per frame, nearest chunks first |
| Direct sun | Traced per pixel near the viewer, soft (rays over the sun's disc, filtered over time); soft from the probes beyond |
| Dynamic lights | 1 to 4 at once (torches, a fire): traced shadows, and bounce through the probes |
| Plants in the rays | Full trees (level of detail 0) with alpha tested leaves that let tinted light through; far away a box with a baked 3D texture of the plant |
| Far plants on screen | Drawn from the same baked 3D texture, in place of SpeedTree's billboard cards |
| Other occluders | glTF models, animals, physics boxes, VR hands. Grass only receives |
| Terrain | Editable, as a full edit system (stored, persisted, physics and plants react) |
| GPUs without ray tracing | The engine starts and looks as it does today (`TreeShade`, billboards), torches light without shadows |
| Frame budget | Quality tiers in `RenderSettings`, one choice per target (desktop, VR) |
| Between runs | Nothing kept, probes are baked again |
| Density field as an SDF | Not used: the density is noise, not a distance, and the BLAS replaces it |

## What exists today

- Vulkan 1.1, `VK_KHR_swapchain` and what OpenXR asks for (`XR_KHR_vulkan_enable`: the engine creates the
  device, so extensions can be added). Transfer, compute and graphics are the same queue. VMA has no buffer
  device address flag.
- One render pass per target, one subpass, straight into the swapchain image, one sample per pixel.
- A chunk is 64³ cubes of 4 m: 256 m a side. Its mesh is a triangle list of `TerrainVertex` (32 bytes, position
  first) in a storage + vertex buffer per slot, at most 32 768 triangles. The triangle count only exists on the
  GPU (`vertexCount` of the slot's indirect command, counted by `atomicAdd` in `marchingcubes.comp.glsl`).
- At the default view distance (768 m): 43 chunks with a surface, about 85 000 plants.
- The sun is `const vec3 sun_direction = normalize(vec3(0.5, 1.0, 0.5))` in six fragment shaders (terrain,
  grass, vegetation, animal, model) and `TreeShade::sun_slope`. The sky is a constant colour. `ViewUBO` has no
  light in it.
- Plants are drawn one call per plant and material, skinning of the animals happens in their vertex shader.

## Overview

```
transfer   uploads (as today)
compute    marching cubes of new and edited chunks
           skinning of the animals near the viewer        -> vertex buffers
           BLAS builds (chunks, species, models), refits (animals)
           TLAS build or refit
           probe update (the frame's ray budget)
graphics   per target: scene passes -> colour, sun, depth  (offscreen)
                       sun visibility (rays, history)
                       composite into the swapchain image, then the UI
```

The compute work is recorded in the command buffer the frame already has, before the graphics submit: the
graphics shaders trace against the TLAS of their own frame.

| Part | Where | Role |
|---|---|---|
| `Sky` | `src/logic/sky/` (in `dynamical_sim`) | Time of day, sun and moon directions and colours, sky model. Context variable |
| `PointLight` | `src/logic/components/` | Colour, intensity, radius; next to a `Transform` |
| `Lights` | `src/renderer/lighting/` | UBO of the frame: sun, moon, sky, the point lights. Set 0 |
| `RayContext` | `src/renderer/context/` | Extensions and features, `enabled()`; nothing else in the renderer asks the device |
| `RayScene` | `src/renderer/lighting/` | BLAS of chunks, species, models, animals; the TLAS; the geometry table rays read |
| `ProbeVolume` | `src/renderer/lighting/` | Bricks of probes per chunk, their atlas and indirection, the update pass |
| `SunVisibility` | `src/renderer/lighting/` | Per target: the rays to the sun, their history, the composite |
| `PlantVolumes` | `src/renderer/lighting/` | The baked 3D textures of each species, for rays and for drawing |
| `TerrainEdits` | `src/logic/terrain/` (in `dynamical_sim`) | Edits of the density, brushes, persistence |

## Sky, sun and moon

`Sky` (context variable, logic side so that headless runs and tests have it): `time` of the day in hours,
`day_length` in seconds of play (1440 by default: an hour a minute, the sun turns a quarter of a degree a
second; 0 stops it), latitude. Scenes set `time` and `day_length`; the scenes of captures and tests stop the
sun. From them the direction of the sun and of the moon, their colours through the atmosphere (white at noon,
orange and dim at the horizon), and the parameters of an analytic sky (zenith and horizon colours from the
sun's elevation, a glow around the sun, a night side).

`sky.glsl` (included by every scene shader and by the probe update) gives `skyColor(direction)`. It replaces the
`sky_color` constants (fog) and answers the rays that miss.

One directional light is traced at a time, the brighter of the sun and the moon; at dusk and dawn its strength
goes through zero as one takes over from the other, so the change does not show.

The `Lights` UBO (set 0, next to `ViewUBO`) carries: direction, colour and angular size of that light, the sky
parameters, the point lights (position, radius, colour), the tier's ranges. The `sun_direction` constants go;
`TreeShade` takes the direction from `Sky` and splats again when the sun has moved by more than a degree
(fallback only).

This part needs no ray tracing and comes first: it is also what a GPU without ray tracing gets.

## Ray tracing context

When the device has them, `RayContext` enables Vulkan 1.2 with `bufferDeviceAddress`, `descriptorIndexing`
(runtime arrays, non uniform indexing), `VK_KHR_acceleration_structure`, `VK_KHR_ray_query` and
`VK_KHR_deferred_host_operations`, and gives VMA `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`. Otherwise
`enabled()` is false and nothing below exists.

- **Shaders**: a module that uses ray queries is not valid on a device without the feature, so each scene
  fragment shader is compiled twice (`-DRAY_TRACING`, `--target-env vulkan1.2`) and the pipeline picks one.
  Set 0 has two layouts for the same reason (a TLAS binding needs the extension).
- **Buffers** that feed a BLAS get `eShaderDeviceAddress | eAccelerationStructureBuildInputReadOnlyKHR`: the
  chunk vertex buffers, the species' and models' vertex and index buffers.

## Acceleration structures

### Chunks

One BLAS per chunk slot, built from the slot's vertex buffer as it is (triangle list, `R32G32B32_SFLOAT` at
offset 0, stride 32, opaque).

The build wants the triangle count on the CPU. After the marching cubes batch the slots' indirect commands are
copied to a host visible buffer; the count is read when the frame's fence is signalled and the BLAS is built
then, `NUM_FRAMES` frames after the mesh. `ChunkLit` marks a chunk whose BLAS is in the TLAS. (Alternative kept
in mind: fill the buffer with NaN and build with the maximum count, no wait but every BLAS sized for 32 768
triangles.)

Built for fast tracing, not compacted while edits may come; a chunk left alone for a few seconds is compacted.
The BLAS follows the slot's life: freed `NUM_FRAMES + 1` frames after the chunk.

### Plants

- **Near** (`plant_geometry_range`, larger than the traced range): an instance of the species' BLAS, built once
  from level of detail 0 when the species loads, compacted. Trunks and branches are opaque geometries, leaf and
  frond materials are not opaque and carry `NO_DUPLICATE_ANY_HIT_INVOCATION`.
- **Far**: an instance of one shared BLAS holding a unit box (procedural geometry), placed over the plant's
  bounds; rays that cross it read the species' baked volume (below).
- A plant changes side with a hysteresis on its distance to the nearest viewer. The shadow of a plant 100 m away
  is already blurred over a metre by the width of the sun: the box takes over where leaves no longer show in
  shadows.

**Leaves let light through.** A shadow ray walks its candidates without committing the leaves:

```
transmittance = 1
for each candidate:
    leaf triangle : alpha = texture(albedo of its material, uv at the hit).a
                    under the cutoff: nothing
                    else: transmittance *= leaf tint x translucency of the species
    plant box     : transmittance *= what the baked volume lets through along the ray
    transmittance under 0.02: stop, in shadow
opaque geometry commits by itself and ends the ray (terminate on first hit): in shadow
```

The order of the leaves does not matter for a product, so no sorting. Under a crown the ground gets green light
through one or two leaves and none through a trunk. The uv comes from the species' vertex and index buffers,
read through buffer references found in the geometry table (`instanceCustomIndex` + geometry index → buffer
addresses, material); the albedo textures of all loaded species are one descriptor array.

**Back lit leaves**: `vegetation.frag.glsl` keeps its subsurface term. The ray to the sun starts a few
centimetres off the surface towards the sun, so a leaf is not its own shadow and the same value serves its lit
side and its back lit side: a leaf seen against the sun glows when the sun reaches it and stays dark deep in
the crown.

Rays through alpha tested cards are the main risk for the frame time in a forest. To measure at milestone 3
(`gpu_times`); `VK_EXT_opacity_micromap` is the known remedy where the GPU has it.

### Plant volumes

Per species, 3D textures over the bounds of level of detail 0, at most 64 × 128 × 64 with mips:

| Texture | Format | Content |
|---|---|---|
| density | `RGBA8` | RGB: how much light a metre of the plant stops, per colour. A: how likely a ray is to hit something there |
| colour | `RGBA8` | Mean albedo of what is in the voxel. A: how much of it is leaf (subsurface) |
| normal | `RGBA8` | RG: mean normal, octahedral. B: baked occlusion |

About 7 MB a species, 300 MB for the 42 of the forest; block compression (the grass bake already writes BC)
when the look is settled.

**Baked** on the GPU when the species' BLAS is ready: for each voxel, short rays in all directions against that
BLAS with the leaf test above. The extinction is `-ln(mean transmittance) / length`; colour and normal are the
means of what the rays hit inside the voxel. Not kept on disk.

**In the rays**: a ray that crosses a plant box is clipped to it and marched in at most 12 steps on a coarse
mip. A shadow ray multiplies its transmittance. A probe ray draws the distance at which it is stopped from the
same densities and commits a hit there (`rayQueryGenerateIntersectionEXT`), lit with the voxel's colour.

**On screen**: beyond the last level of detail with geometry, `VegetationPass` draws a plant as its box, one
instanced draw per species. The fragment shader marches the density from the box's near face (32 steps at most,
the start jittered per pixel), stops where the opacity passes a threshold drawn per pixel, writes that depth,
and shades the voxel's colour and normal like a leaf. A plant fades from its mesh to its box by a dither.
The trees stay round from any side, from above too, and their look does not turn with the camera as cards do.
The distance of that change is the target's; the one of the rays is shared.

The quality to expect at 60 to 120 m (a 15 m tree is 90 pixels tall at 120 m in the desktop window, 128 voxels)
is the open point of this part: to judge against the billboards in `vegetation_test` with `lods`. Without ray
tracing there is no bake: billboards stay.

### Models, boxes, hands, animals

- **glTF models**: a BLAS per file (`ModelManager`), an instance per entity.
- **Physics boxes and spheres, VR hands**: instances of a unit box and a unit sphere, scaled.
- **Animals** within the traced range: a compute pass skins the positions into a vertex buffer per animal (the
  matrices are already in a storage buffer), the BLAS is refitted every frame and rebuilt now and then. Beyond
  the range an animal casts nothing. The tier caps their number.

### TLAS

All of the above, about 90 000 instances at the default view distance. Rebuilt when the set of instances changes
(chunks, plants changing side), refitted when only transforms moved. Each instance has a mask: terrain, plants,
static models, dynamic things, so a ray can leave some out (probe rays skip the dynamic things).

## Probes

### Layout

A chunk with a surface owns a **brick** of 65³ probes, one every 4 m: one on every density sample of the chunk,
so a probe's density is known. The faces are shared with the neighbours: trilinear filtering inside a chunk
never leaves its brick. Shared probes are traced by both chunks with the same seeded rays, so they agree; a
copy of the faces is the fix if seams show.

Per probe, 28 bytes:

| Texture | Format | Content |
|---|---|---|
| 3 × irradiance | `RGBA16F` | L1 spherical harmonics of the light arriving, one texture per colour |
| direct | `RGBA8` | RGB: how much of the sun (or moon) reaches the probe, tinted by the leaves crossed. A: how much of the sky it sees |

7.7 MB a brick, 330 MB at the default view distance. (20 bytes are possible: the mean light in
`B10G11R11`, the three directions as ratios to it in `RGBA8`; 240 MB. To do if memory is short.) Bricks live in
3D atlas textures (pages of 8 bricks, 130³ texels). A small wrapping 3D texture of chunk coordinates (16³,
`R16UI`) gives the brick of any world position, so plants, animals and models, which are not drawn per chunk,
find theirs; the terrain and the grass get the brick pushed per draw like their soil layer.

### Which probes are traced

From the chunk's densities and plants, on the CPU when the chunk is generated or edited:

- **In the rock** (density > 0): never traced. They take the mean of their valid neighbours, so that filtering
  does not pull black into the ground's light.
- **Active**: in the air within 8 m of the ground or of a plant's bounds. Traced.
- **Open air**: the rest. They get the sky and the unobstructed sun, without rays.

Most of a brick is rock or open air; a rough count is 15 000 to 25 000 active probes a chunk, under a million
loaded.

### First value

A new brick starts with today's look: the direct term from the crown depths of `TreeShade` at the probe, the
irradiance from the sky and ground colours the shaders use now. A chunk appears lit and shaded by its trees at
once, then the rays correct it. Its first updates blend fast.

### Update

Every frame a compute pass takes a list of active probes, nearest chunks first, new bricks and probes near an
edit or a torch before the others, until the tier's ray budget is spent. Per probe:

1. `rays_per_probe` (32 to 128) directions, a spherical Fibonacci set turned by a rotation drawn per frame.
2. Each ray finds its closest hit (leaves pass the alpha test, then stop the ray with the probability of their
   opacity; plant boxes as above). A miss reads `skyColor`.
3. At the hit: albedo (terrain: a colour from the slope and the soil map; plants: the leaf or bark texture;
   models: their base colour) times the light arriving there: the sun through a shadow ray with tinted
   transmission, each torch in reach through a shadow ray, and the irradiance of the probes around the hit as
   they are now. That last term is what makes light bounce more than once, one bounce more per update.
4. The rays are projected on L1 and blended into the stored value (`hysteresis`, 0.9 to 0.97, lower for new
   bricks and near torches).
5. A few rays to the sun give the direct term, blended the same way.

At 64 rays a probe and a million rays a frame, a million active probes are all refreshed in about 60 frames:
under a second, a quarter of a degree of sun. Chunks near the viewers come round more often than far ones.
These are estimates to replace by measures; the ray budget is the first thing the tiers set.

### Reading

`probeLight(position, normal)` in `lighting.glsl`: the brick from the indirection (or pushed), the position
moved 1 m along the normal (less light through thin walls), hardware trilinear on the four textures, L1
evaluated for the normal. Used by every scene shader for the ambient term. Grass reads it at the top of the
blades with the ground's normal; the blade shadows it bakes itself stay.

Known limit: probes 4 m apart let light through walls thinner than that (thin overhangs). The remedy, if it
shows, is a depth per probe as in DDGI, at a cost per probe that this layout cannot pay everywhere.

## Shading

| Light | Near (within `traced_range`) | Far |
|---|---|---|
| Sun or moon | Rays per pixel over the light's disc, tinted transmission, filtered over time | The probes' direct term |
| Sky and bounces | Probes | Probes |
| Torches | Direct light, one hard shadow ray per pixel and light in reach | Same, lights are few and short ranged |

### The sun's visibility

Soft shadows need rays spread over the sun's disc, and a filter to hide their noise. The filter works on a
screen sized image of the visibility, so the sun's light is kept apart until the end of the frame:

1. **Scene passes** render into offscreen images instead of the swapchain: `colour` (`RGBA16F`, everything but
   the directional light), `sun` (`B10G11R11`, what that light adds where nothing hides it: diffuse, specular,
   subsurface) and the depth. Fog is applied to both. The scene shaders do not trace the sun.
2. **`SunVisibility`**, per target and eye: the world position from the depth. Within `traced_range`,
   `sun_rays` rays (1 or 2) to points of the disc drawn from a blue noise that changes every frame. Beyond, the
   probes' direct term. Between the two, over the last fifth of the range, a blend.
3. **History**: last frame's visibility is fetched where the point was on screen (the eye's previous view
   projection; the world does not move), refused where the depth does not match, clamped to what the 3 × 3
   pixels around say now so that the shadow of a moving thing does not trail, and blended with the new rays
   (a tenth new). A small blur that respects depth fills what has no history.
4. **Composite**: `colour + sun × visibility` into the swapchain image. The UI pass comes after.

Per eye in VR: two histories. Reprojection follows the head from the depth alone, no motion vectors are needed
for a static world; moving things (animals, boxes, hands) rely on the clamp. This is not an anti-aliasing of
the image: only the visibility is filtered, the picture stays as sharp as today.

The offscreen step is also where tone mapping can go later.

Torches are small sources: their shadows are hard, traced in the scene fragment shaders (the only use of the
ray traced variants of those shaders), not filtered.

`TreeShade` is no longer read when ray tracing is on, except for the first value of the bricks.

## Tiers

`RenderSettings::Lighting`, one per target, in the debug menu:

| | off | low | medium | high |
|---|---|---|---|---|
| `traced_range` | 0 (probes only) | 25 m | 60 m | 150 m |
| `sun_rays` a pixel | 0 | 1 | 1 | 2 |
| `plant_geometry_range` | 60 m | 60 m | 120 m | 250 m |
| Shadowed torches a pixel | 0 | 1 | 2 | 4 |
| Animals with a BLAS | 0 | 8 | 32 | 128 |

and for the whole frame: `probe_rays_per_frame`, `rays_per_probe`. Values to tune on the measures of each
milestone. The plant range is shared (one TLAS): the larger of the two targets'.

## Terrain edits

A project of its own, with its own document when it starts; what the lighting needs from it is one event.

- **Store**: `TerrainEdits` (context variable, `dynamical_sim`) keeps a delta of the density per edited chunk,
  on the same 67³ samples, allocated on the first edit. Density = generator + delta. Kept when the chunk
  unloads, written to disk per chunk (`saves/<world>/edits/`), read back when it loads.
- **Brushes**: add and remove in a sphere, smooth, flatten; from the VR hand and from the mouse. Samples on a
  chunk's border belong to several chunks and are written in all of them. Undo as a list of strokes.
- **One source of density**: everything that reads `TerrainGenerator::sample` or a `ChunkVolume` (terrain
  colliders, `GroundField`, `TerrainField`, forests) goes through generator + edits, loaded or not.
- **What reacts**, from a `ChunkEdited` signal with the bounds of the stroke:
  - `TerrainSys` meshes the chunk again into a new slot and swaps when it is ready (frames in flight keep the old)
  - the collider cells that overlap are dropped and rebuilt, sleeping bodies there are woken
  - plants whose ground moved are put back on it or removed, the soil and grass height maps are baked again
  - lighting: the chunk's BLAS is rebuilt, the probes of the stroke's surroundings are classified again and
    jump the queue with a fast blend

## Fallback

Without ray tracing: `Sky`, the `Lights` UBO and unshadowed torches work; shading is today's, straight into the
swapchain, with `TreeShade` following the sun and billboards far away. No probes. Terrain edits work. Headless
and training runs have no renderer and are not concerned.

## Order of the work

| | Milestone | Shows |
|---|---|---|
| L1 | `Sky`, `Lights` UBO, sun out of the shaders, procedural sky, `PointLight` unshadowed, `TreeShade` follows | Time of day on any GPU |
| L2 | `RayContext`, shader variants, chunk BLAS with the count readback, TLAS; offscreen targets, `sun` image, `SunVisibility` with one ray to the middle of the sun, composite | Hills shadow themselves, hard |
| L3 | Species BLAS, geometry table, tinted leaves; models, boxes, hands | Dappled green light, back lit leaves |
| L4 | Rays over the disc, history, blur | Soft shadows that widen with distance |
| L5 | `ProbeVolume`: bricks, classification, update, `probeLight`, far sun from the probes, first value | Bounced light, soft far shade |
| L6 | Torches: traced shadows, bounce | A torch in a forest at dusk |
| L7 | `PlantVolumes`: bake, far plants as boxes in the rays, then on screen | Whole view distance in the rays, round far trees |
| L8 | Animals: skinning pass, refits | Herds cast shadows |
| L9 | Moon and night, tiers tuned per target | A full day in VR |
| E1 | `TerrainEdits`, one source of density, re-mesh, lighting hook | A debug stroke relights |
| E2 | Colliders and ground fields | Bodies fall in the hole |
| E3 | Plants, soil and grass maps | |
| E4 | Brushes in VR and on the desktop, persistence, undo | |

L1 to L5 in order; L6 to L9 and the E track can be taken in any order after L5 (E1's lighting hook after L2).

## Tests and captures

- `LIGHT.*` in `tests/unit/lighting.cpp`: the sky model (sun path, colours), L1 projection and evaluation
  mirrored on the CPU, probe classification on known densities, brick allocation and the wrapping indirection.
- `EDIT.*` in `tests/unit/terrain_edits.cpp`: brushes, shared border samples, save and load, the one source of
  density.
- Captures wait for `LightingStats::probes_pending == 0` and a settled history (as they wait for the species),
  with the sun stopped. Fixed-clock runs update a fixed number of probes a frame with seeds from the frame
  number. Goldens with ray tracing depend on the GPU; the fallback's are the reference of the test suite.

## Open

- How good a far tree looks from its volume, against the billboards (L7).
- Memory of the probes at 4 m (330 MB) and of the plant volumes (300 MB): the compact layouts if needed.
- What rays through leaves cost in a thick forest, for both VR eyes (L3).
