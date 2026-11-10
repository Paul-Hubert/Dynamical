# Vegetation

Plants of the Tautavel Unity project (SpeedTree 8 `.st` files), converted to a library the engine draws with its own
shader. No wind, no colliders, no shadow maps: the shade under the plants is a map of crown shadows
([forest.md](forest.md), which also has the forests these plants grow in).

## Building the library

The library is generated, not committed (`resources/vegetation/` is ignored: 1.5 GB, licensed models).

```bash
python tools/speedtree_import.py --source <Tautavel> [--lfs <lfs objects>]
```

- `--source`: a Tautavel checkout (the folder holding `Assets/`), or its bare git repository (`--ref` picks the commit).
- `--lfs`: the Git LFS object store, when the source only has pointers for the textures (a bare repository, a checkout
  without LFS). On the Gitea host: `gitea-docker/data/git/repositories/paleomob3d/tautavel.git` and
  `gitea-docker/data/gitea/lfs`.
- `--only <regex>` converts some species and keeps the rest, `--list` lists them, `--max-texture` (2048) downscales.

Needs numpy and Pillow (`build/venv` has both). A full run takes a few minutes; textures already written are kept.

Every `.st` under `Assets/` is taken once (the same file exists in several folders). 80 species come out: 37 trees,
16 shrubs, 27 ground plants. Three files of `Assets/Tautavel/Plants` are left out, their textures are not in the
repository. The `.spm` (SpeedTree Modeler sources) cannot be read, only the `.st` they were exported to.

The engine reads `./resources/vegetation` next to the working directory, else the source tree's
(`vegetationDirectory()`): the build does not copy the library.

## Files

```
resources/vegetation/
  library.json           index: id, name, category (ground < 1 m, shrub < 4 m, tree), height, radius, triangles, lods
  species/<id>.json      bounds, alpha cutoff, materials, levels of detail and their draw calls
  species/<id>.bin       vertices (28 bytes each) of every level, then indices (uint32, relative to their level)
  textures/<name>.png    shared between species
```

Vertex: position (3 float, meters, Y up), normal (4 int8 normalized, w: sign of the bitangent), tangent (4 int8
normalized, w: ambient occlusion), uv (2 float, v down).

Materials have up to four maps, as SpeedTree 8 exports them for Unity: color (alpha: opacity), normal, extra
(r: smoothness, g: metallic, b: ambient occlusion), subsurface.

## The .st format

Reverse engineered, enough for the above. `SpeedTree___`, then nested tables: a count and that many offsets relative
to the table, down to raw values. Strings are a length (with the terminator) and the characters.

Root table: version (8), ?, last level is the billboard (bool), levels of detail, collision objects, materials,
?, wind, ?.

- Level of detail: vertices (count, 140 bytes each), indices (count, uint32), draw calls (count, 4 uint32 each:
  material, geometry hint 0 branch / 2 leaves / 4 billboard, first index, index count).
- Vertex, 35 floats: anchor (3), offset (3), offset in the next level (3), normal (3), tangent (3), binormal (3),
  uv (2), lightmap uv (2), color (3), wind data (6), ambient occlusion (1), blend weight (1), 2 unused here.
  The position is anchor + offset, in feet (`scaleFactor` of the `.st.meta`, 0.3048). v goes up.
- Material: name, two sided (bool), billboard (bool), 5 maps (used, file name, color).
- The billboard level is 9 cards: 8 around the plant and one seen from above, on a 3 x 3 atlas.

Unity's `.st.meta` gives the scale, the alpha cutoff (0.33) and the screen height at which each level hands over to
the next.

## Engine

- `src/logic/vegetation/`: `VegetationLibrary` (context variable, no Vulkan), the `Plant` component (species index,
  optional forced level), and two scene systems:
  - `VegetationScatter`: a few plants on every chunk crossing the surface, from the chunk's density samples,
    deterministic for a seed, destroyed with the chunk. Slopes over `max_slope` are left bare.
  - `VegetationLineup`: one plant of each selected species in a row, and with `"lods": true` one copy per level of
    detail behind it. Positions are logged.
  Both select species with `species` (ids) or `categories`, cut down to `max_species` with `seed`.
- `VegetationPass` (`"vegetation"` in a scene's passes): draws every `Transform` + `Plant`. A species is loaded when
  a plant first uses it or when a `VegetationPreload` lists it, files and mip levels on the taskflow executor (6
  species at a time), Vulkan objects on the main thread. One level of detail per plant and render target, from
  Unity's screen heights times `lod_bias` (2). Plants outside a cone around the desktop camera's view are skipped.
  Back faces of bark are culled, leaves are drawn from both sides.
- `vegetation.vert.glsl` / `vegetation.frag.glsl`: alpha test, normal map (x and y only, z rebuilt, as Unity reads
  them), baked occlusion on the ambient light and half on the sun, light through the leaves from the subsurface map,
  a little specular from the smoothness. Billboard cards fade with their angle to the camera and are lit as if they
  faced it, as Unity's SpeedTree8 shader does.
- Textures: mip levels are filtered on the CPU, colors weighted by alpha, transparent texels filled with the colors
  around them (SpeedTree atlases are black there), alpha scaled to keep the coverage of the alpha test. Leaf atlases
  stop at 1/32 of their size: under that leaves blend with the twigs next to them.
- Debug menu, Vegetation: counts, toggles for the pass, normal maps, subsurface and culling, the strength of the
  shade under the plants, LOD bias, forced level.

## Looking at it

```bash
cd build && ./RelWithDebInfo/dynamical.exe scene=vegetation_test
```

6 tree species picked with `var.veg_seed`, 40 per chunk (`var.per_chunk`), and the same 6 in a row 40 m in front of
the spawn with their levels of detail behind (`var.lods=false` to drop those). `var.max_species=0` takes every tree
of the library: about 20 MB of video memory per 2048 texture, uncompressed.

## Not done

- Wind (the data is in the `.st`, dropped by the importer), colliders, real shadows.
- Cross fade between levels of detail: they pop.
- Instancing: one draw call per plant and material. No culling for the headset.
- Texture compression (BC7 / BC5): the library is PNG, decoded and mipped at load.
- Vegetation that is not SpeedTree (the FBX and prefab plants of the Unity project).
