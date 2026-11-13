# WorldView

`WorldViewSys` in [`src/view/world_view.cpp`](../../../src/view/world_view.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.post · Parameters: yes

Draws the bodies of the scene's worlds (docs/physical-agents/scenes.md): for every body and shape of every world it
keeps one host entity with `Transform` and `ModelRenderable`, so the renderer keeps reading the host registry only.
World i sits at `gridOffset(i) x spacing` on the XZ plane; boxes and spheres scale the unit primitives of
`resources/models/primitives`; capsules are generated per (radius, half height); dynamic bodies are drawn between their
last two ticks with the WorldStepper alpha.

## Behaviour

**tick** (nothing without `Worlds` or `ModelManager`), for each world (or those listed in `worlds`), with offset
`gridOffset(index, count, spacing)` (row-major on a ceil(sqrt(n)) grid, centred on the origin):

1. Every created `Ground`: the primitive box, size `Ground::size`, top at `top_y`, grey-green.
2. Every `Transform` + created `RigidBody`: pose from `PhysicsPose` mixed with alpha (else the `Transform`), plus
   `rotation x offset`; colour from `ModelRef::color`, else a hash of the entity, overridden by `colors[index % n]`;
   model: box (scale 2 x half_extent), sphere (2 x radius) or a capsule mesh `capsule:<r>:<h>` made once with
   `ModelManager::add(capsuleMesh(r, h))`.
3. Every `Transform` + `WorldShape` (drawn-only shapes, the ragdolls'): the same.
4. The host entity of each (world, source entity) is made on first sight (`Transform`, `ModelRenderable` with model and
   colour, `WorldViewLink`) and its `Transform` set to offset + pose every tick. Host entities whose source was not seen
   this tick are destroyed.

**finish**: destroys every host entity it made.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `worlds` | array of int, or "all" | every world | indices of the worlds drawn (anything but an array: all) |
| `spacing` | float | 6.0 | m between worlds on the grid (0: all at the origin) |
| `colors` | array of [r, g, b(, a)] | none | world i's bodies take colors[i % n] instead of their own |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Worlds](../components/Worlds.md) | context | read | tick | every world (or those of "worlds") |
| [WorldStepperState](../components/WorldStepperState.md) | context | read | tick | alpha between the last two ticks (1 without it) |
| [Ground](../components/Ground.md) | component | read | tick (world registries) | created grounds: a box of size, top at top_y |
| [Transform](../components/Transform.md) | component | read | tick (world registries) | position, rotation of bodies and shapes |
| [RigidBody](../components/RigidBody.md) | component | read | tick (world registries) | created bodies: shape, half_extent, offset |
| [PhysicsPose](../components/PhysicsPose.md) | component | read | tick (world registries) | prev / current pose mixed with alpha |
| [ModelRef](../components/ModelRef.md) | component | read | tick (world registries) | color of the body or shape |
| [WorldShape](../components/WorldShape.md) | component | read | tick (world registries) | drawn-only shapes (type, half_extent) of the ragdolls |
| [ModelManager](../components/ModelManager.md) | context | readwrite | tick | load() of the primitive box / sphere; add() of a capsule mesh per (radius, half height) |
| [ModelC](../components/ModelC.md) | component | write+create | tick (ModelManager::load / add) | primitives on first load, generated capsules (GPU upload) |
| [Transform](../components/Transform.md) | component | readwrite+create+destroy | tick, finish | one host entity per (world, body or shape): made on first sight, posed at offset + pose every tick, destroyed when the source goes and in finish |
| [ModelRenderable](../components/ModelRenderable.md) | component | readwrite+create+destroy | tick, finish | model and colour of the host entity, set when it is made |
| [WorldViewLink](../components/WorldViewLink.md) | component | readwrite+create+destroy | tick, finish | which world and entity the host entity draws |

## Side effects

- Uploads generated capsule meshes to the GPU through ModelManager::add (Transfer), once per size.
- Creates and destroys host entities as bodies appear and go; destroys all of them in finish.

## Order and dependencies

In host.post, after WorldStepper (pre) has stepped the worlds this frame: it reads the poses of this frame's ticks and
the alpha. The colour and the model of a host entity are fixed when it is made. AnimalAgentView and SphereView use the
same grid (`gridOffset`) and alpha.

## Used in scenes

<!-- generated -->
- [`animal_demo_view.scene.json`](../../../resources/scenes/animal_demo_view.scene.json): `host.post`
- [`animal_terrain_view.scene.json`](../../../resources/scenes/animal_terrain_view.scene.json): `host.post`
- [`animal_view.scene.json`](../../../resources/scenes/animal_view.scene.json): `host.post`
- [`devita_view.scene.json`](../../../resources/scenes/devita_view.scene.json): `host.post`
- [`ik_walk_slope.scene.json`](../../../resources/scenes/ik_walk_slope.scene.json): `host.post`
- [`ik_walk_terrain.scene.json`](../../../resources/scenes/ik_walk_terrain.scene.json): `host.post`
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.post`
- [`rig_anim.scene.json`](../../../resources/scenes/rig_anim.scene.json): `host.post`
- [`sandbox_view.scene.json`](../../../resources/scenes/sandbox_view.scene.json): `host.post`
- [`sphere_view.scene.json`](../../../resources/scenes/sphere_view.scene.json): `host.post`
- [`sphere_view_heuristic.scene.json`](../../../resources/scenes/sphere_view_heuristic.scene.json): `host.post`
- [`test_boxes_view.scene.json`](../../../resources/scenes/test_boxes_view.scene.json): `host.post`
- [`vr_roller.scene.json`](../../../resources/scenes/vr_roller.scene.json): `host.post`
- [`walker_demo_terrain_view.scene.json`](../../../resources/scenes/walker_demo_terrain_view.scene.json): `host.post`
- [`walker_terrain_view.scene.json`](../../../resources/scenes/walker_terrain_view.scene.json): `host.post`
- [`walker_view.scene.json`](../../../resources/scenes/walker_view.scene.json): `host.post`
- [`wheel_view.scene.json`](../../../resources/scenes/wheel_view.scene.json): `host.post`

## Tests

GPU: `V2.1 test_boxes_view at frame 300 matches its golden` (`tests/capture/worlds_boxes.capture.json`), `P2 check:
scripted clicks pause and single-step WorldStepper`.
