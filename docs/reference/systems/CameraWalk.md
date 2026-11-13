# CameraWalk

`CameraWalkSys` in [`src/view/camera_walk.cpp`](../../../src/view/camera_walk.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: yes

Walks the desktop camera over the marching cubes terrain at eye height, along a line that goes from open land
through a forest and out again, by a path that keeps clear of trunks and of branches at eye height: the videos of the
forests (`tools/forest_video.py`, docs/forest.md, docs/tautavel-walk.md) and the reference profile of the engine
(docs/profiling.md). It waits for the land to be loaded, then walks at a steady speed, sways its view, turns to herds
and looks down at bare soil when asked, and stops at the end of the line (optionally ending the process). Its state is
the [CameraWalk](../components/CameraWalk.md) context variable.

## Behaviour

- **preinit**: emplaces `CameraWalk`.
- **init**:
  1. With a `TerrainGenerator`: a `TerrainField` (the marching cubes surface of the densities, CPU) is the ground.
  2. With `soil` > 0 and soil drawn (`RenderSettings::soil`, or no settings): a `SoilField` from
     `RenderSettings::soil_params`.
  3. No `start` / `heading` given: `plan()` (needs a `ForestField` and the generator) scores lines every 48 m within
     `search` m of the origin and 16 headings: open for the first and last `lead` m (default min(45, 18 % of the
     distance)), forest cover ≥ 0.8 between, mixed stands preferred, gentle heightfield slopes, near the origin; plus
     `soil` x the bare soil its open ends cross. The 30 best (60 with herds, which add `herdsBy`: herd homes beside
     the line, from `HerdSys::describe`) are scored again on the real ground (`TerrainField::top`, 4 m steps, 3 m
     to each side); the best is logged.
  4. Still no line: straight ahead of the spawn camera.
  5. Eye height above the ground at the start; `CameraWalk` start / heading / distance; `place()`.
- **tick**:
  1. Not started, after 4 frames, when `terrainReady`, `vegetationReady`, `animalsReady` (with herds) and
     `lightingReady`, and (with a `ForestField`) no chunk with a volume is still unplanted (`ChunkVolume` without
     `ChunkForest`): `route()` then `started`, `profile::message("CameraWalk : started")`.
     - `route()`: a 0.5 m grid over a 72 m wide corridor; plants over 1.2 m tall (`Transform` + `Plant`, species from
       `VegetationLibrary`) are walls at the trunk and cost what they reach at eye height ±0.6 m (from their first LOD's
       geometry by height and direction, read once per species; else from the species' size); steep ground costs;
       Dijkstra from the start to the far end; 60 smoothing passes.
  2. Not started, finished, or dt ≤ 0: `place()` only. With herds, it also waits while animals load.
  3. `DY_FRAME_NAMED("Walk")`; h = min(dt, 0.1); steers critically damped (0.3 s, at most 70°/s) toward the path point
     4 m ahead; moves `speed` x h; the eye follows the ground (0.3 s) and the climb is smoothed (1.2 s).
  4. `progress` along the line; at `distance` or the end of the path: `finished`,
     `profile::message("CameraWalk : finished")`, and with `exit` sets `Input::EXIT`.
  5. With soil or herds, `look()`: the nearest herd within `herds` m and less than 115° off the heading is looked at
     (logged when it changes); bare soil 4 m ahead makes it look down at a grazing angle, less when a herd is near.
  6. `place()`: `Camera::position` = (x, eye, z); yaw = direction + sway x sin(2π t / 13) (blended toward the look);
     pitch = 0.6 atan(climb) + 0.03 + 0.05 sin(2π t / 9) (blended toward the look).

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `distance` | float | 280 | length of the line, m |
| `speed` | float | 4 | walking speed, m/s |
| `eye_height` | float | 1.7 | eye above the ground, m |
| `sway` | float | 8 | degrees the view swings from side to side |
| `start` | [x, z] | — | start of the line (with `heading`); else it is planned |
| `heading` | float | — | degrees, 0 toward -z, 90 toward -x |
| `search` | float | 1200 | radius of the plan's search around the origin, m |
| `lead` | float | 0 | metres of open land at each end; 0: min(45, 18 % of the distance), at most 40 % |
| `soil` | float | 0 | weight of the bare soil the open ends cross in the plan; > 0 also looks down at it |
| `herds` | float | 0 | m: herds whose home is this close to the line count in the plan, the camera turns to the nearest |
| `exit` | bool | false | end the process at the end of the walk |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [CameraWalk](../components/CameraWalk.md) | context | readwrite+create | preinit (emplace), init, tick, look | start / heading / distance in init; started, progress, finished in tick |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init, plan, groundHeight | TerrainField for the ground, heightfield slopes for planning; std::shared_ptr<TerrainGenerator>, optional |
| [RenderSettings](../components/RenderSettings.md) | context | read | init | soil and soil_params: the SoilField the line is scored on (with soil > 0) |
| [Camera](../components/Camera.md) | context | readwrite | init, place | init reads the spawn position and yaw when no line is given or found; place writes position, yaw, pitch every tick |
| [ForestField](../components/ForestField.md) | context | read | plan, tick | cover and stand of the nodes for the line; with it the walk waits until every chunk is planted; std::shared_ptr<ForestField> |
| [HerdSys](../components/HerdSys.md) | context | read | plan (herdsBy) | def() and describe(cell) for the herd homes near a candidate line; stored as HerdSys*; with herds > 0 |
| [HerdStats](../components/HerdStats.md) | context | read | tick (look) | list of herds: the nearest one ahead is looked at; with herds > 0 |
| [VegetationLibrary](../components/VegetationLibrary.md) | context | read | tick (route, reach) | species height / radius and, once per species, the geometry of its first LOD read from disk |
| [Transform](../components/Transform.md) | component | read | tick (route) | position, yaw and scale of the plants near the line |
| [Plant](../components/Plant.md) | component | read | tick (route) | species of the plants near the line |
| [ChunkVolume](../components/ChunkVolume.md) | component | read | tick | chunks with a volume... |
| [ChunkForest](../components/ChunkForest.md) | component | read | tick | ...but no ChunkForest yet: the forest is still being planted |
| [ChunkC](../components/ChunkC.md) | component | read | tick (terrainReady) | chunks not yet generated (without ChunkGenerated) keep the walk waiting |
| [ChunkGenerated](../components/ChunkGenerated.md) | tag | read | tick (terrainReady) | excluded: a generated chunk is not waited for |
| [ChunkMeshPending](../components/ChunkMeshPending.md) | tag | read | tick (terrainReady) | a chunk waiting for its mesh keeps the walk waiting |
| [ChunkDestroying](../components/ChunkDestroying.md) | tag | read | tick (terrainReady, unplanted view) | excluded: chunks on their way out are not waited for |
| [VegetationStats](../components/VegetationStats.md) | context | read | tick (vegetationReady) | species_loading == 0 (true without it) |
| [AnimalStats](../components/AnimalStats.md) | context | read | tick (animalsReady) | species_loading == 0 (true without it); only with herds > 0 |
| [LightingStats](../components/LightingStats.md) | context | read | tick (lightingReady) | settled() (true without ray tracing) |
| [Input](../components/Input.md) | context | write | tick | sets Input::EXIT at the end of the walk with "exit": the process ends |

## Side effects

- Log lines "CameraWalk : from x z heading ..." (plan), soil / herds scores, and each herd it turns to.
- profile::message("CameraWalk : started") when the route is made and ("CameraWalk : finished") at the end: tools/profile_run.py --between cuts on them; DY_FRAME_NAMED("Walk") marks every frame of the walk; CPU zone "Route the walk".
- Reads species geometry files through VegetationLibrary::loadSpecies / loadGeometry (once per species, main thread).
- With "exit", ends the process at the end of the walk (Input::EXIT).

## Order and dependencies

- After PlayerControl (overrides the camera; init reads its spawn camera when no line is found), after ChunkManager /
  ChunkGenerator (`TerrainGenerator` in their preinit) and Forest; after Herds with `herds` (the `HerdSys*` context
  variable and `HerdStats`).
- Waits on what the renderer publishes a frame late (`VegetationStats`, `AnimalStats`, `LightingStats`), as captures do,
  so frame n of two runs is the same view.

## Used in scenes

<!-- generated -->
- [`forest_torch.scene.json`](../../../resources/scenes/forest_torch.scene.json): `host.pre`
- [`forest_walk.scene.json`](../../../resources/scenes/forest_walk.scene.json): `host.pre`
- [`tautavel_walk.scene.json`](../../../resources/scenes/tautavel_walk.scene.json): `host.pre`
- [`tautavel_walk_torch.scene.json`](../../../resources/scenes/tautavel_walk_torch.scene.json): `host.pre`

## Tests

None in `tests/`; `tools/forest_video.py` and `tools/profile_run.py --between "CameraWalk : started" "CameraWalk : finished"` rely on it.
