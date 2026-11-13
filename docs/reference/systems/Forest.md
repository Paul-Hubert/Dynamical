# Forest

`ForestSys` in [`src/logic/vegetation/forest.cpp`](../../../src/logic/vegetation/forest.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `ChunkManager` and `ChunkGenerator` · Parameters: yes

Forests in patches on the gentle parts of the terrain and open grass elsewhere ([forests](../../forest.md)). It
makes the world-anchored [ForestField](../components/ForestField.md) (forest cover 0..1 and the kind of forest,
the stand, on an 8 m lattice) and plants every generated chunk with the candidates of the definition's layers that
stand on it, so that the plants never depend on the order the chunks load in. Each chunk's plants are entities with
a [Transform](../components/Transform.md) and a [Plant](../components/Plant.md), listed in the chunk's
[ChunkForest](../components/ChunkForest.md) and destroyed with it.

## Behaviour

**preinit**: makes the storages of Plant and ChunkForest and connects `onChunkForestDestroyed` to
`on_destroy<ChunkForest>`.

**init**
- Loads (or finds) the [VegetationLibrary](../components/VegetationLibrary.md) (`vegetationLibrary`).
- Needs the `std::shared_ptr<TerrainGenerator>` of the ChunkGenerator; without it, logs an error and does nothing.
- The definition: the `definition` parameter as an inline object, or the file `./resources/<definition>`
  (default `forest/tautavel.forest.json`); `seed`, `patch_size`, `coverage`, `max_slope` of the parameters
  override the file's. `ForestDefinition::parse`: field params, `stands` (named, each with its layers) and
  `everywhere` layers (all stands and the open land); a layer is `where` (forest, edge, open, all), `spacing`,
  `fill`, `clump`, `max_slope`, `scale`, `species` (weights or a list). Species the library lacks are left out with
  a warning. Any error is logged and leaves the system without layers.
- Makes the ForestField (patch noise quantiles measured so that `coverage` of the land is forest, with `edge` of
  it thinning out; slope of the heightfield over `slope_reach` on each side, no forest over `max_slope`, fading over
  `slope_fade`), `insert_or_assign`s it as `std::shared_ptr<ForestField>` and a
  [VegetationPreload](../components/VegetationPreload.md) with every species of the layers. Logs a summary.

**tick**: chunks with [ChunkC](../components/ChunkC.md) and [ChunkVolume](../components/ChunkVolume.md), not yet
planted (no ChunkForest) and not [ChunkDestroying](../components/ChunkDestroying.md): two a frame, or all of them
in fixed-clock runs (`RunArgs::deterministic()`, so a frame never depends on timing). `scatter(chunk)`:
- emplaces the ChunkForest (left empty when the volume does not have the 67³ samples);
- the highest surface of each column of samples (`ChunkHeights`);
- per layer, the candidates of the grid cells over the chunk (`forestCandidates`: one jittered candidate per cell
  of `spacing` m anchored to the world, kept with probability `fill` x a function of the cover (`forest`: c²,
  `edge`: 4c(1 - c), `open`: (1 - c)², `all`: 1), in clumps by a value noise when `clump` > 0, only in its stand
  (stands mix a little where they meet), species by weight, scale in range, forest trees smaller where the cover
  thins). Every draw is made whether the plant is kept or not.
- a candidate is kept when it is inside the chunk's cubes, its column has a surface in the cubes the chunk owns
  (the chunk above or below has the others), its four neighbours too, and the ground's gradient is under the
  layer's `max_slope`;
- an entity at the surface height, sunk by min(2 % of the species' height, 0.15 m) x scale x (1 + 4 x gradient),
  yawed and scaled, with its Plant; its id is pushed to the chunk's list.

**signal** `on_destroy<ChunkForest>` (the chunk is destroyed by the chunk systems): every valid plant of its list is
destroyed.

**finish**: disconnects the signal.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `definition` | string or object | `forest/tautavel.forest.json` | resources path of the definition, or the definition itself |
| `seed` | uint | the definition's (1) | noise seed of the field and the layers |
| `patch_size` | float | the definition's (320) | m: size of the forests and of the open land between them |
| `coverage` | float | the definition's (0.5) | part of the gentle land under forest |
| `max_slope` | float | the definition's (24) | degrees of land slope where no forest grows |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Plant](../components/Plant.md) | component | write+create | preinit (storage), tick (scatter) | one entity per kept candidate: its species index |
| [Transform](../components/Transform.md) | component | write+create | tick (scatter) | on the ground of the chunk, sunk a little, yawed, scaled |
| [ChunkForest](../components/ChunkForest.md) | component | write+create | tick (scatter) | on the chunk: the list of its plants (empty for a chunk without full samples) |
| [ChunkForest](../components/ChunkForest.md) | component | read | signal on_destroy<ChunkForest> (onChunkForestDestroyed) | the plants of the chunk being destroyed |
| [Plant](../components/Plant.md) | component | destroy | signal on_destroy<ChunkForest> (onChunkForestDestroyed) | the chunk's plant entities are destroyed with it |
| [Transform](../components/Transform.md) | component | destroy | signal on_destroy<ChunkForest> (onChunkForestDestroyed) | with the plant entities |
| [ChunkC](../components/ChunkC.md) | component | read | tick | chunks to plant (view) and their origin |
| [ChunkVolume](../components/ChunkVolume.md) | component | read | tick | the densities: highest surface per column, slope from the neighbours |
| [ChunkDestroying](../components/ChunkDestroying.md) | tag | read | tick | excluded: a chunk on its way out is not planted |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | std::shared_ptr<TerrainGenerator> of the ChunkGenerator: the heightfield the ForestField takes the land's slope from |
| [ForestField](../components/ForestField.md) | context | write+create | init | insert_or_assign of the std::shared_ptr<ForestField> (cover and stands), read by CameraWalk and HerdSys |
| [VegetationPreload](../components/VegetationPreload.md) | context | write+create | init | every species of the definition's layers, for VegetationPass to load before any plant needs them |
| [VegetationLibrary](../components/VegetationLibrary.md) | context | read+create | init (vegetationLibrary), tick (scatter) | loaded from resources/vegetation on first use; species ids for the definition, heights for the sinking |
| [RunArgs](../components/RunArgs.md) | context | read | tick | fixed-clock runs plant every pending chunk in the frame, else two a frame |

## Side effects

- Connects to on_destroy<ChunkForest> in preinit (onChunkForestDestroyed), disconnects in finish.
- Reads ./resources/forest/tautavel.forest.json (or `definition`) in init, and resources/vegetation/library.json through vegetationLibrary when no system loaded it yet.
- Creates the plant entities, destroys them with their chunk.
- Logs info (stands, layers, species, patch size), a warning per species the library lacks, an error without a terrain generator or with a bad definition (the system then does nothing).

## Order and dependencies

- After `ChunkGenerator`, which makes the TerrainGenerator in its preinit and the ChunkVolumes in its tick.
- Plants are drawn by `VegetationPass`, which loads the preloaded species first; `TreeShade` splats their crowns;
  `CameraWalk` reads the ForestField and the ChunkForests (trunks to route around); `HerdSys` reads the field and
  the plants.
- A chunk destroyed takes its plants with it through the signal, whoever destroys it.

## Used in scenes

<!-- generated -->
- [`forest.scene.json`](../../../resources/scenes/forest.scene.json): `host.pre`
- [`forest_herds.scene.json`](../../../resources/scenes/forest_herds.scene.json): `host.pre`
- [`forest_herds_watch.scene.json`](../../../resources/scenes/forest_herds_watch.scene.json): `host.pre`
- [`forest_torch.scene.json`](../../../resources/scenes/forest_torch.scene.json): `host.pre`
- [`forest_walk.scene.json`](../../../resources/scenes/forest_walk.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`
- [`tautavel_walk.scene.json`](../../../resources/scenes/tautavel_walk.scene.json): `host.pre`
- [`tautavel_walk_torch.scene.json`](../../../resources/scenes/tautavel_walk_torch.scene.json): `host.pre`

## Tests

- `FOREST.1` to `FOREST.3` in [`tests/unit/forest.cpp`](../../../tests/unit/forest.cpp): the cover in patches on
  gentle land, layers that follow the cover and stands whatever the chunk that asks, and the system planting each
  chunk once on its ground and taking the plants away with it (`FOREST.map`, skipped, writes a map).
