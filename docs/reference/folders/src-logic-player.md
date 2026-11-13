# src/logic/player

The physical players (docs: [players.md](../../players.md)), ported from Unity Dynamical's `GenericPlayer`,
`FPSPlayer`, `VRPlayer`, `Hand`, `Grabber`, `Thrower` and `GenericPlayerInput`: a player is an entity with a dynamic
`RigidBody` at its eyes; what it can do is the components it carries, each with one system, and the systems only meet
through those components. It also holds the player's side of riding an animal (getting on, steering, the seat). Part of
the simulation library: no window or headset here (the device systems `MouseKeyboardInput`, `ControllersInput` and
`PlayerModels` are the viewer's, in `src/view/player_input.*`). Every system is a host system of a scene's `pre` list;
what acts on bodies does so in a `FixedSteps` step, at the 60 Hz physics rate.

## Files

| File | What |
|---|---|
| [`player.h`](../../../src/logic/player/player.h) | The player components (`PlayerView`, `LookTurn`, `DesktopView`, `ThirdPerson`, `PlayerLegs`, `BodyDrive`, `VRTracking`, `VRBody`, `Hand`, `Grabber`, `Grabbed`, `Ungrabbable`, `Holder`, `Held`, `Prop`), `makeFPSPlayer`, `makeVRPlayer`, `registerPlayer` |
| [`player_systems.h`](../../../src/logic/player/player_systems.h) | The system classes, the `PlayerGroups` sub groups and the `PlayerCollision` context variable, the shared helpers (`playerBodyId`, `bodyEntity`, `bodyMass`, `playerCollision`, `setPlayerGroup`, `clearPlayerGroup`) |
| [`player_body.cpp`](../../../src/logic/player/player_body.cpp) | The makers, `PlayerSpawn`, `LookTurn`, `PlayerLegs`, `PlayerViewpoint`, and the registration of every system of the folder |
| [`vr_player.cpp`](../../../src/logic/player/vr_player.cpp) | `VRPlayerInput`, `VRBody`, `VRHands` |
| [`grab.cpp`](../../../src/logic/player/grab.cpp) | `Grab` (VR hands lock onto what they touch, throw) and `Hold` (the FPS pick up) |
| [`body_drive.cpp`](../../../src/logic/player/body_drive.cpp) | The shared helpers (body ids, collision groups) and `BodyDrives` (Jolt six degrees of freedom motors) |
| [`generic_input.h`](../../../src/logic/player/generic_input.h) | `GenericPlayerInput` and its four implementations (`MouseKeyboardInput`, `ControllersInput`, `VRPlayerInput`, `ScriptedInput`), `playerInput()` |
| [`generic_input.cpp`](../../../src/logic/player/generic_input.cpp) | Movement and view axes of the devices, stick dead zone, `playerInput()` (scripted first, else the active device used last) |
| [`mount.h`](../../../src/logic/player/mount.h) | Riding: `Mountable`, `MountCollider`, `Rider`, `saddleOf`, the classes of `Mountables`, `Mounting`, `MountSteering`, `RiderSeat`, `MountSpawn`, `registerMounts` |
| [`mount.cpp`](../../../src/logic/player/mount.cpp) | The riding systems; the saddle and trunk measured on an animal's skin |

## Systems registered here

All `Host` scope, registered by `registerPlayer()` (called from `src/scene/system_registry.cpp`) and `registerMounts()`.

| System | What |
|---|---|
| [PlayerSpawn](../systems/PlayerSpawn.md) | one FPS or VR player on the ground, boxes to grab, optionally a flat ground |
| [VRPlayerInput](../systems/VRPlayerInput.md) | the headset (`VRInput`) to the VR players' `VRTracking` and `VRPlayerInput` |
| [LookTurn](../systems/LookTurn.md) | the view axis turns `PlayerView` |
| [VRBody](../systems/VRBody.md) | the body follows the head, the play area turns, poses held without focus |
| [PlayerLegs](../systems/PlayerLegs.md) | stand, walk, jump with forces; the players' collision groups |
| [VRHands](../systems/VRHands.md) | controller poses to the hands' drives, failsafe, legs' forces to the hands |
| [Grab](../systems/Grab.md) | a hand locks onto what it touches while the grip is held, throws |
| [Hold](../systems/Hold.md) | the FPS player picks up, holds, drops, throws |
| [BodyDrives](../systems/BodyDrives.md) | a Jolt `SixDOFConstraint` with velocity motors per `BodyDrive` |
| [PlayerViewpoint](../systems/PlayerViewpoint.md) | after `Physics`: the desktop `Camera` and the `VRRig` on the player |
| [Mountables](../systems/Mountables.md) | animals of some species get a `Mountable` and, near a rider, a kinematic collider |
| [Mounting](../systems/Mounting.md) | jump gets on the animal touched, or off |
| [MountSteering](../systems/MountSteering.md) | the walk axis along the view is the animal's `AnimalSteer` |
| [RiderSeat](../systems/RiderSeat.md) | the rider's body held over the saddle by a capped spring force |
| [MountSpawn](../systems/MountSpawn.md) | a row of animals to ride at the spawn |

Usual scene order (`pre`): `PlayerSpawn`, `MountSpawn`, the input systems, `LookTurn` or `VRPlayerInput` + `VRBody`,
`Mounting`, `MountSteering`, `SteeredWalk`, `AnimalAnimation`, `AnimalGait`, `Mountables`, `PlayerLegs`, `RiderSeat`,
`VRHands`, `Grab`, `Hold`, `BodyDrives`, `PlayerModels`, `RigidBody`, `TerrainColliders`, `Physics`, `PlayerViewpoint`.
The physics steps run in that same order (legs, seat, hands, grab, hold, drives).

## Components and context variables defined here

| Name | Kind | What |
|---|---|---|
| [PlayerView](../components/PlayerView.md) | component | where the player looks (yaw, pitch) |
| [LookTurn](../components/LookTurn.md) | component | the view axis turns the view |
| [DesktopView](../components/DesktopView.md) | tag | the desktop camera is this player's eyes |
| [ThirdPerson](../components/ThirdPerson.md) | component | ... from behind |
| [PlayerLegs](../components/PlayerLegs.md) | component | stand, walk, jump; ground state, collision group |
| [BodyDrive](../components/BodyDrive.md) | component | a body driven to a pose relative to another |
| [VRTracking](../components/VRTracking.md) | component | head and hands of the headset, stage space |
| [VRBody](../components/VRBody.md) | component | the body under the head, play area yaw, focus state |
| [Hand](../components/Hand.md) | component | a rigid body hand following a controller |
| [Grabber](../components/Grabber.md) | component | a hand that grabs and throws |
| [Grabbed](../components/Grabbed.md) | component | on what hands hold |
| [Ungrabbable](../components/Ungrabbable.md) | tag | nothing grabs or picks it up |
| [Holder](../components/Holder.md) | component | the FPS pick up |
| [Held](../components/Held.md) | component | on what a Holder holds |
| [Prop](../components/Prop.md) | component | a box made by PlayerSpawn |
| [PlayerCollision](../components/PlayerCollision.md) | context | the group filter of every player |
| [MouseKeyboardInput](../components/MouseKeyboardInput.md) | component | keyboard and mouse input |
| [ControllersInput](../components/ControllersInput.md) | component | gamepad input |
| [VRPlayerInput](../components/VRPlayerInput.md) | component | headset controllers input |
| [ScriptedInput](../components/ScriptedInput.md) | component | input written by tests and scripts |
| [Mountable](../components/Mountable.md) | component | an animal that can be ridden |
| [MountCollider](../components/MountCollider.md) | component | on the kinematic body of a Mountable |
| [Rider](../components/Rider.md) | component | a player that rides |

## Worth knowing

- `playerInput(reg, entity)` (`generic_input.h`): the input of a player as a `GenericPlayerInput&`: its `ScriptedInput`
  when it has one, else the active device with the latest `last_action`, else an input that asks for nothing. Buttons
  are states; systems that need presses keep the last state (`Holder::was_interact`, `Rider::was_jump`).
- `makeFPSPlayer`, `makeVRPlayer` (`player.h`): the two lists of components a player is (tests call them directly).
- `PlayerGroups` / `PlayerCollision` (`player_systems.h`): one Jolt collision group per player (given by `PlayerLegs`),
  sub groups `body`, `left_hand`, `right_hand`, `held_left`, `held_right`, `held_both`, `held_body`, `mount`; a
  `GroupFilterTable` says which do not collide (body / hands, a hand / what it holds, body / held body, body and hands /
  the animal ridden). `setPlayerGroup`, `clearPlayerGroup`.
- `playerBodyId` (the Jolt body of an entity's `RigidBody`), `bodyEntity` (the entity of a Jolt body made by
  `RigidBodySys`, through its user data, checked against recycled ids), `bodyMass`.
- `saddleOf(reg, mount)` (`mount.h`): the saddle of a `Mountable` in world space, on its joint when it has one.
- A hand and a held body are the same thing to the physics: a dynamic body with a `BodyDrive` anchored on the player.
  `VRHands` and `Hold` only write the target; `BodyDrives` owns the constraint.

## Dependencies

- `src/physics/` (`PhysicsWorld`, `physicsWorld()`, `FixedSteps`, `TerrainNeeds`, `PhysicsPose`, Jolt queries and
  constraints), `src/logic/components/` (`Transform`, `RigidBody`, `Ground`, `Camera`, `VRInput`, `VRRig`, `VRPose`).
- `src/logic/animals/` (`AnimalLibrary`, `Animal`, `AnimalPose`, `AnimalGait`, `analyseGait`, `AnimalSteer`,
  `AnimalBotProxy`, `AnimalSkin`, `headingOf`) for riding.
- `src/logic/terrain/` (`TerrainGenerator`, `MarchingCubes`), `src/logic/agents/ground_field.h` (`GroundField`,
  `TerrainField`) for the spawns.
- `src/logic/systems/system.h`, `src/scene/system_registry.h`; `src/util/log.h`.
- Used by `src/view/` (`player_input.*`: the device systems, `PlayerModels`; `PlayerControl` stands aside while a
  `DesktopView` exists) and by `src/logic/animals/steer.*` (`SteeredWalk`, `AnimalBotSteer` follow the `AnimalSteer`
  the riding systems write).

## Design docs

- [docs/players.md](../../players.md): the players, input, riding, the body, the drive, VR, FPS, tests.
- [docs/animals.md](../../animals.md): the animal library, herds and the procedural walk the ridden animals use.

Tests: `PLAYER.*` in [`tests/unit/player.cpp`](../../../tests/unit/player.cpp), `MOUNT.*` in
[`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp), `HERD.2` in [`tests/unit/animals.cpp`](../../../tests/unit/animals.cpp).
