# AnimalAgentView

`AnimalAgentViewSys` in [`src/view/animal_view.cpp`](../../../src/view/animal_view.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: yes

The view of the physical animals (docs/physical-agents/reports/animal-agents.md): the skinned mesh of the animal
library follows the ragdoll of each world (joints that are bodies take their body's pose, the others ride on it), a
camera follows the animal of one world from behind its commanded heading, a panel shows its command, speed and
episode. It can also drive that animal with a scripted list of orders, lay a chequer on the ground to see it move, and
log a line per frame for `tools/animal_orders_video.py`. The scene lists the `animals` pass and no AnimalAnimation:
this system writes the poses.

## Behaviour

- **Constructor**: reads the parameters; `orders` may be an array or a string holding the JSON array (a scene
  variable), empty string = none.
- **preinit**: creates the storages of `Animal`, `AnimalPose`, `Transform` (the animal pass views them from workers);
  `animalLibrary(reg)` makes and loads the `AnimalLibrary` context variable when missing.
- **tick** (nothing without `Worlds`):
  1. With `mesh`, for each world whose `AnimalBot` context variable is built: on first sight, finds the body's species
     in the library and loads it (`species()`; a failure marks the world and it is skipped from then on), binds the body
     to the skeleton (`animalBodyBind`), creates a host entity with `Transform`, `Animal { species }`, `AnimalPose`,
     `AnimalSkin`. Every tick: joints from the segments (`skeletonFromSegments`), made relative to the trunk, skin
     matrices (`skinMatrices`); `Transform` at the trunk + the world's grid offset (`WorldViewSys::gridOffset` with
     `spacing`).
  2. The viewed world (`world`, nothing when out of range or not built):
     - `orders`: the time of the tour advances with the command's own time (it runs on through a fall); the current
       order (seconds, speed, radius) sets `wanted_speed = speed x walk_speed`, `wanted_turn = ±1 / (|radius| x length)`
       (0 straight) and pushes the next draw to never; the first 0.05 s of an episode also set `speed` / `turn` at once.
       An order of speed 0 lets the draws come back (`until` = now).
     - `marks` > 0: 800 box tiles (made once) in a 40 x 40 chequer of `marks` m cells around the trunk, on the ground
       under it.
     - `log`: opens the file on the first frame with a CSV header, then a line per frame (frame, tick, time, order,
       command, yaw, position, velocity, trunk height ratio, walk speed, length), flushed.
     - `camera`: the focus jumps when it is more than 3 sizes + 3 m away, else eases (6 / s); the heading eases (0.6 /
       s) to `command.yaw`; the eye is `distance x size + 0.5` m from the aim (ground + half the body height), swung
       `azimuth` degrees, `height x body height + 0.1` up.
     - ImGui window "Animal": species, bodies, mass, tick, decisions, command, speed, trunk height, reward (`AgentIO`).

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `world` | int | 0 | world followed by the camera and the panel |
| `distance` | float | 2.4 | camera distance, in sizes of the animal (max of length and height) |
| `height` | float | 0.5 | camera height, in body heights |
| `azimuth` | float | 70 | degrees from behind the commanded heading |
| `camera` | bool | true | drive the desktop camera |
| `mesh` | bool | true | draw the skinned meshes |
| `spacing` | float | 0 | world grid spacing, as WorldView's |
| `orders` | array or string | none | [[seconds, speed (in walk speeds), radius (in lengths, > 0 left, < 0 right, 0 straight)], ...] |
| `marks` | float | 0 | m: chequer cell size on the ground (0: none) |
| `log` | string | "" | file to write a CSV line per frame to |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Worlds](../components/Worlds.md) | context | read | tick | every world, and the viewed one |
| [AnimalLibrary](../components/AnimalLibrary.md) | context | readwrite+create | preinit, tick (animalLibrary, species) | made and loaded from the library directory when missing; species() loads the viewed species from disk on first use |
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | tick (world registries) | built ragdoll: body, position / rotation of the segments, groundUnder, command (written by "orders"), tick, decisions, rewards |
| [AgentIO](../components/AgentIO.md) | context | read | tick (viewed world's registry) | reward shown |
| [Transform](../components/Transform.md) | component | readwrite+create | tick | one skin entity per world (at the trunk + the world's grid offset); the chequer tiles |
| [Animal](../components/Animal.md) | component | write+create | tick | species index of the skin entity |
| [AnimalPose](../components/AnimalPose.md) | component | write+create | tick | world joints from skeletonFromSegments and skin matrices, every tick |
| [AnimalSkin](../components/AnimalSkin.md) | tag | write+create | tick | marks the skin entity: only a ragdoll's drawn skin |
| [ModelManager](../components/ModelManager.md) | context | readwrite | tick | load("./resources/models/primitives/box.gltf") for the chequer (marks > 0) |
| [ModelC](../components/ModelC.md) | component | write+create | tick (ModelManager::load) | the primitive box on its first load |
| [ModelRenderable](../components/ModelRenderable.md) | component | write+create | tick | 800 grey chequer tiles (marks > 0) |
| [FrameClock](../components/FrameClock.md) | context | read | tick | frame number written to the log |
| [Camera](../components/Camera.md) | context | write | tick | position, yaw, pitch behind the commanded heading (camera true) |

## Side effects

- Loads the animal library index (preinit) and the viewed species (first tick) from disk on the main thread; logs which.
- With "log", opens the file once (truncated) and appends a CSV line every frame, flushed.
- ImGui window "Animal".
- Creates host entities (one skin per world with a built bot, 800 tiles with marks) that are never destroyed (no finish).

## Order and dependencies

- After WorldStepper (poses of this frame's ticks) and PlayerControl; in `player_deer` with `camera` false before
  AnimalBotSteer and the player systems (PlayerViewpoint owns the camera).
- Writes `WalkCommand` of the viewed world's bot between ticks: the next tick's task and observation read the scripted
  order.
- The animal pass draws the `Animal` + `AnimalPose` entities; with `AnimalSkin` they are a ragdoll's skin.

## Used in scenes

<!-- generated -->

## Tests

None directly (`AA.*` cover the bodies and agents); `tools/animal_video.py`, `tools/animal_orders_video.py`,
`tools/ride_video.py --ride deer` film it.
