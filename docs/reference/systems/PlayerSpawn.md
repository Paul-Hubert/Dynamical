# PlayerSpawn

`PlayerSpawnSys` in [`src/logic/player/player_body.cpp`](../../../src/logic/player/player_body.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, before the input systems and `RigidBody` · Parameters: yes

Puts the player of a `player_*` scene into the world: one FPS player (keyboard, mouse, gamepad, legs, look, desktop camera, hold, riding) or one VR player (headset input, legs, body under the head, two hands that grab), standing on the ground at a given place, with a few boxes to pick up in front of it. All the work is in the makers `makeFPSPlayer` and `makeVRPlayer`, which tests call directly.

## Behaviour

- **Constructor**: reads the parameters (below). `kind` other than `"vr"` means FPS.
- **preinit**: with `ground: true`, creates an entity with a `Ground` of 1200 x 10 x 1200 m (top at y = 0). This has
  to come before `RigidBody`'s init, which makes the Jolt bodies of the grounds.
- **init**:
  - The ground height at a point is `TerrainGenerator::surfaceHeight(x, z)` when the scene has a
    `std::shared_ptr<TerrainGenerator>` context variable, else 0.
  - The eyes are 1.8 m over the ground at `position` (`[x, z]`).
  - FPS: `makeFPSPlayer(reg, eyes, yaw)`: an entity with `Transform` at the eyes, a dynamic `RigidBody` capsule
    (radius 0.108 m, half height 0.342 m, offset 0.45 m down so its top is at the eyes, 70 kg, `lock_rotation`, friction
    0.2, restitution 0), `MouseKeyboardInput`, `ControllersInput`, `PlayerView` (yaw), `PlayerLegs`, `LookTurn`,
    `DesktopView`, `Holder`, `Rider`. With `third_person` > 0 it also gets a `ThirdPerson` with that distance.
  - VR: `makeVRPlayer(reg, eyes, yaw)`: the same capsule, `VRPlayerInput`, `PlayerView`, `PlayerLegs`, `VRTracking`,
    `VRBody` (play area yaw = `yaw`, head at (0, 1.8, 0) of the stage). Two hand entities (left, right): `Hand`
    (player, side, rest pose 0.22 m to the side, 0.45 m down, 0.3 m ahead of the head), `Transform` at that pose,
    a dynamic box `RigidBody` (half extents 0.04 x 0.05 x 0.09 m, 1.5 kg), a `BodyDrive` anchored on the player
    targeting the rest pose, a `Grabber`. `VRBody::hands` lists them.
  - `props` boxes on an arc of 100 degrees in front of the player (0.9 to 1.4 m away), 0.15 to 0.36 m in size, 0.6 m
    plus their size above the ground, each with `Transform` (scale = size), a dynamic box `RigidBody` and a `Prop`.
    Every fourth box is grey and weighs 40 kg (it weighs on an arm); the others 300 kg/m3 x size3, orange to yellow.
- **tick**: nothing.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `kind` | string | `"fps"` | `"vr"` makes a VR player, anything else an FPS one |
| `position` | [x, z] | [0, 0] | where the player stands (array of exactly two numbers, else ignored) |
| `yaw` | float | 0 | rad, the player's view yaw (and the VR play area's) |
| `props` | int | 8 | boxes in front of the player |
| `ground` | bool | false | a flat `Ground` at y = 0, for scenes without a terrain |
| `third_person` | float | 0 | m: the FPS player gets a `ThirdPerson` camera this far behind |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Ground](../components/Ground.md) | component | write+create | preinit | with `ground: true`: a new entity with a 1200 x 10 x 1200 m Ground (top at y = 0), before RigidBody's init makes the grounds |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | `std::shared_ptr<TerrainGenerator>`: `surfaceHeight` gives the ground under the player and the boxes; 0 without one |
| [Transform](../components/Transform.md) | component | write+create | init (makeFPSPlayer, makeVRPlayer) | the player at its eyes, the hands, the boxes |
| [RigidBody](../components/RigidBody.md) | component | write+create | init (makeFPSPlayer, makeVRPlayer) | the player's 70 kg capsule (lock_rotation), the 1.5 kg hand boxes, the boxes |
| [MouseKeyboardInput](../components/MouseKeyboardInput.md) | component | write+create | init (makeFPSPlayer) | the FPS player's keyboard and mouse |
| [ControllersInput](../components/ControllersInput.md) | component | write+create | init (makeFPSPlayer) | the FPS player's gamepad |
| [VRPlayerInput](../components/VRPlayerInput.md) | component | write+create | init (makeVRPlayer) | the VR player's controllers |
| [PlayerView](../components/PlayerView.md) | component | write+create | init (makers) | yaw set to the spawn's |
| [PlayerLegs](../components/PlayerLegs.md) | component | write+create | init (makers) | default legs |
| [LookTurn](../components/LookTurn.md) | component | write+create | init (makeFPSPlayer) | the view axis turns the FPS player's view |
| [DesktopView](../components/DesktopView.md) | tag | write+create | init (makeFPSPlayer) | the desktop camera follows the FPS player |
| [ThirdPerson](../components/ThirdPerson.md) | component | write+create | init | with `third_person` > 0 on the FPS player: the camera that far behind |
| [Holder](../components/Holder.md) | component | write+create | init (makeFPSPlayer) | the FPS player's pick up |
| [Rider](../components/Rider.md) | component | write+create | init (makeFPSPlayer) | the FPS player can ride |
| [VRTracking](../components/VRTracking.md) | component | write+create | init (makeVRPlayer) | empty tracking of the VR player |
| [VRBody](../components/VRBody.md) | component | write+create | init (makeVRPlayer) | play area yaw = spawn yaw, head at 1.8 m, its two hand entities |
| [Hand](../components/Hand.md) | component | write+create | init (makeVRPlayer) | two hand entities, left and right, with their rest pose |
| [BodyDrive](../components/BodyDrive.md) | component | write+create | init (makeVRPlayer) | each hand driven to its rest pose, anchored on the player |
| [Grabber](../components/Grabber.md) | component | write+create | init (makeVRPlayer) | each hand grabs |
| [Prop](../components/Prop.md) | component | write+create | init | `props` boxes, coloured (the fourth of each four grey and heavy) |

## Side effects

- Creates entities: the player (and its two hands for VR) and `props` box entities in init; one Ground entity in preinit with `ground: true`.
- No physics bodies itself: RigidBody (a later system) makes the Jolt bodies of the RigidBody components it adds.

## Order and dependencies

Before `RigidBody` (which makes the Jolt bodies of the components it adds, and in its init the `Ground`) and after the terrain generator is in the context (`ChunkGenerator` or whatever emplaces `std::shared_ptr<TerrainGenerator>`) if the player should stand on the terrain. Before every other player system, which expect the components it makes.

## Used in scenes

<!-- generated -->
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.pre`
- [`player_fps.scene.json`](../../../resources/scenes/player_fps.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`
- [`player_vr.scene.json`](../../../resources/scenes/player_vr.scene.json): `host.pre`

## Tests

No unit test runs the system itself; `makeFPSPlayer` and `makeVRPlayer` are used by every test of [`tests/unit/player.cpp`](../../../tests/unit/player.cpp) (`PLAYER.1` to `PLAYER.9`) and [`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp) (`MOUNT.2` to `MOUNT.5`). Captures [`player_fps.capture.json`](../../../tests/capture/player_fps.capture.json), [`player_vr.capture.json`](../../../tests/capture/player_vr.capture.json), [`player_horse.capture.json`](../../../tests/capture/player_horse.capture.json), [`player_deer.capture.json`](../../../tests/capture/player_deer.capture.json).
