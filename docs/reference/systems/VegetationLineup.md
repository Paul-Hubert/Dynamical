# VegetationLineup

`VegetationLineupSys` in [`src/logic/vegetation/vegetation_scatter.cpp`](../../../src/logic/vegetation/vegetation_scatter.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `ChunkGenerator` · Parameters: yes

One plant of each selected species of the [vegetation](../../vegetation.md) library in a row on the terrain, to
compare them side by side; optionally, behind each, a copy held at each of its levels of detail and one drawn from
its baked volume. The positions are logged.

## Behaviour

**init** (nothing else; `tick` is empty):
- The [VegetationLibrary](../components/VegetationLibrary.md) (loaded on first use) and the same species
  selection as [VegetationScatter](VegetationScatter.md) (`species`, else `categories`, cut to `max_species` with
  `seed`). Nothing without species or without the `std::shared_ptr<TerrainGenerator>` of the ChunkGenerator.
- The row is centred on `origin.x`, along +X, each species taking its crown's diameter + `spacing`.
- For each species: row 0 at z = `origin.y` with `lod` -1 (by distance); with `lods`, rows behind it (every
  diameter + `spacing` further along -Z) held at levels 0, 1, ... (one per level of the species) and a last one at
  `Plant::volume`. The ground height comes from the density function itself (`TerrainGenerator::sample`, two
  columns of 30 samples round the heightfield's guess), else the heightfield's height: chunks need not be loaded.
- One info line per plant.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `categories`, `species`, `max_species`, `seed` | | as VegetationScatter | the selection |
| `origin` | [x, z] | [0, -40] | middle of the row |
| `spacing` | float | 4 | m between two crowns |
| `lods` | bool | false | one copy behind each plant per level of detail, and one from the baked volume |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [VegetationLibrary](../components/VegetationLibrary.md) | context | read+create | init (vegetationLibrary) | the species, their radius, height and number of levels |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | the ground height at each plant from the density function itself (no chunk needed) |
| [Transform](../components/Transform.md) | component | write+create | init | the plant on the ground |
| [Plant](../components/Plant.md) | component | write+create | init | species, and `lod` -1 (by distance), a held level, or Plant::volume |

## Side effects

- Creates the lineup's entities once in init (never destroyed by the system).
- Logs one info line per plant (species, level, position, height).

## Order and dependencies

- After `ChunkGenerator`, whose preinit makes the TerrainGenerator (systems init after every preinit).

## Used in scenes

<!-- generated -->
- [`vegetation_test.scene.json`](../../../resources/scenes/vegetation_test.scene.json): `host.pre`

## Tests

- None directly; `VEG.1` in [`tests/unit/vegetation.cpp`](../../../tests/unit/vegetation.cpp) covers the library it
  reads.
