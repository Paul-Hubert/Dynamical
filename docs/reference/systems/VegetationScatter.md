# VegetationScatter

`VegetationScatterSys` in [`src/logic/vegetation/vegetation_scatter.cpp`](../../../src/logic/vegetation/vegetation_scatter.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `ChunkGenerator` · Parameters: yes

Test scattering of the [vegetation](../../vegetation.md) library: a few random plants of the selected species on
every generated chunk that crosses the surface, to look at the species in the world. Deterministic for a seed
whatever the order the chunks load in; each chunk's plants are listed in its
[ChunkPlants](../components/ChunkPlants.md) and destroyed with it.

## Behaviour

**constructor**: reads the parameters; the selection keys (`species`, `categories`, `max_species`, `seed`) are kept
apart.

**preinit**: storages of Plant and ChunkPlants, `onChunkPlantsDestroyed` connected to `on_destroy<ChunkPlants>`.

**init**: the [VegetationLibrary](../components/VegetationLibrary.md) (loaded on first use), the species
selected: the `species` ids (unknown ones warned and left out), else every species of the `categories`, cut to
`max_species` by a partial Fisher-Yates with `std::mt19937(seed)` (not `std::shuffle`, whose result depends on the
standard library), sorted. Logs the selection.

**tick** (nothing without species or with `per_chunk` <= 0): every chunk with ChunkC and ChunkVolume, without
ChunkPlants and not ChunkDestroying, is scattered in the same tick. `scatter(chunk)`:
- emplaces ChunkPlants (empty when the volume is not 67³);
- a `std::mt19937` seeded with a hash of the chunk position and `seed`; `per_chunk` draws, each of x, z inside the
  chunk's cubes, a species, a yaw, a scale (all drawn whether the plant is kept or not);
- the highest surface of the column inside the cubes the chunk owns (densities bilinear in x and z), the surfaces
  one sample away on each side, the normal from them: kept when `normal.y >= cos(max_slope)`;
- an entity with a Transform at the surface (sunk by min(2 % of the species' height, 0.15 m) x scale x (1 + 4(1 -
  normal.y))), yawed and scaled, and a Plant.

**signal** `on_destroy<ChunkPlants>`: the chunk's plants are destroyed. **finish**: disconnects it.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `per_chunk` | int | 6 | draws per chunk |
| `seed` | uint | 1 | of the draws and of the species selection |
| `scale` | [min, max] | [0.8, 1.2] | uniform scale range |
| `max_slope` | float | 35 | degrees of ground slope |
| `categories` | [string] | ["tree"] | every species of these categories (`ground`, `shrub`, `tree`) |
| `species` | [string] | none | exactly these ids (before `categories`) |
| `max_species` | int | 0 | keeps this many of the selection (0: all); the same params pick the same species in VegetationLineup |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Plant](../components/Plant.md) | component | write+create | preinit (storage), tick (scatter) | one entity per kept draw: its species index |
| [Transform](../components/Transform.md) | component | write+create | tick (scatter) | on the surface of the chunk, sunk a little, yawed, scaled |
| [ChunkPlants](../components/ChunkPlants.md) | component | write+create | tick (scatter) | on the chunk: the list of its plants |
| [ChunkPlants](../components/ChunkPlants.md) | component | read | signal on_destroy<ChunkPlants> (onChunkPlantsDestroyed) | the plants of the chunk being destroyed |
| [Plant](../components/Plant.md) | component | destroy | signal on_destroy<ChunkPlants> (onChunkPlantsDestroyed) | the chunk's plants are destroyed with it |
| [Transform](../components/Transform.md) | component | destroy | signal on_destroy<ChunkPlants> (onChunkPlantsDestroyed) | with the plant entities |
| [ChunkC](../components/ChunkC.md) | component | read | tick | chunks to plant (view), their position for the seed and the origin |
| [ChunkVolume](../components/ChunkVolume.md) | component | read | tick | densities: the surface of a column and the slope around it |
| [ChunkDestroying](../components/ChunkDestroying.md) | tag | read | tick | excluded from the view |
| [VegetationLibrary](../components/VegetationLibrary.md) | context | read+create | init (vegetationLibrary), tick (scatter) | species selection by id or category; species heights for the sinking |

## Side effects

- Connects to on_destroy<ChunkPlants> in preinit, disconnects in finish.
- Reads resources/vegetation/library.json through vegetationLibrary when no system loaded it yet.
- Creates plant entities, destroys them with their chunk.
- Logs the selected species (info) and a warning per unknown id.

## Order and dependencies

- After `ChunkGenerator` (ChunkVolume). The plants are drawn by `VegetationPass`.

## Used in scenes

<!-- generated -->
- [`vegetation_test.scene.json`](../../../resources/scenes/vegetation_test.scene.json): `host.pre`

## Tests

- `VEG.2` in [`tests/unit/vegetation.cpp`](../../../tests/unit/vegetation.cpp): scattered plants stand on the
  surface, repeat for a seed and go with their chunk.
