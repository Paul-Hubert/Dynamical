# Mounting

`MountingSys` in [`src/logic/player/mount.cpp`](../../../src/logic/player/mount.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `LookTurn`, before `MountSteering` and `PlayerLegs` · Parameters: no

Getting on and off. A press of jump while the player's body touches a free `Mountable`'s collider gets it on: the legs go limp, the animal gets an `AnimalSteer` (and so becomes the rider's to steer: `SteeredWalk` or `AnimalBotSteer`), the rider's body goes through the animal's. A press of jump while riding gets it off with a hop to the animal's left; it is also put off when the animal is gone or the seat is lost.

## Behaviour

- **preinit**: gets the `PhysicsWorld`; makes the pools of `Mountable`, `MountCollider`, `AnimalSteer`.
- **tick**, for every `Rider` + `PlayerLegs` + `Transform`:
  - A press is jump now and not `was_jump`.
  - `left_timer` counts down; at 0 the collider of the animal left has its collision group cleared (bumped into again).
  - Riding: **gone** (the animal is no longer valid or lost its `Mountable` or `Transform`) or **lost** (seated,
    `blend >= 1`, and the eyes more than `lost` from the saddle + `seat`) dismounts without a hop; a press dismounts
    with a hop.
  - Not riding, on a press: **touched** collides the player's capsule (at its place, then 0.6 m lower: where the legs
    would be) with the world, max separation `reach`, ignoring its own bodies; the first hit that is a `MountCollider`
    of a `Mountable` without a rider is mounted.
- **mount**: `Rider::mount`, `blend = 0`, `seat_known = false`; `Mountable::rider`; `PlayerLegs::limp` and
  `jump_locked`; the animal gets `AnimalSteer` with its current heading and speed 0; the collider goes into the
  player's `mount` sub group (no collision with its body and hands).
- **dismount**: legs back (`limp` false, `jump_locked`), `AnimalSteer` removed, `Mountable::rider` cleared, the collider
  remembered in `left` for 0.6 s; with a hop the body's velocity = `seat_velocity` + the animal's rotation x
  (`-hop`, 1, 0).

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [ScriptedInput](../components/ScriptedInput.md) | component | read | helper playerInput | answers for the player when present (tests, scripts) |
| [MouseKeyboardInput](../components/MouseKeyboardInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [ControllersInput](../components/ControllersInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [VRPlayerInput](../components/VRPlayerInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit, tick | contact queries of the player's capsule, collision groups, the hop's velocity |
| [Rider](../components/Rider.md) | component | readwrite | tick, mount, dismount | reach, seat, lost, hop, blend read; mount, was_jump, blend, seat_known, left, left_timer written |
| [PlayerLegs](../components/PlayerLegs.md) | component | readwrite | tick, mount, dismount | group read; limp and jump_locked set on getting on and off |
| [Transform](../components/Transform.md) | component | read | tick, mount, dismount (and helper saddleOf) | the player's eyes, the animal's heading and saddle |
| [Mountable](../components/Mountable.md) | component | readwrite | preinit (storage), tick, touched, mount, dismount | `rider` set and cleared; collider read; saddle via saddleOf |
| [MountCollider](../components/MountCollider.md) | component | read | preinit (storage), touched | the animal of a touched collider body |
| [AnimalSteer](../components/AnimalSteer.md) | component | readwrite+create+destroy | preinit (storage), mount (emplace_or_replace), dismount (remove) | set, not read (the schema has no write+create+destroy): emplaced or replaced on the animal with its current heading and speed 0; removed when the rider gets off |
| [AnimalPose](../components/AnimalPose.md) | component | read | tick (helper saddleOf) | the joint that carries the saddle |
| [RigidBody](../components/RigidBody.md) | component | read | touched, mount, dismount, tick (helpers playerBodyId, bodyEntity) | the player's and the collider's body ids, the entity of a touched body |
| [PlayerCollision](../components/PlayerCollision.md) | context | read+create | touched, mount, tick (helpers playerCollision, setPlayerGroup, clearPlayerGroup) | the touch query ignores the player's bodies; the ridden collider goes into sub group `mount`, cleared 0.6 s after getting off |

## Side effects

- `CollideShape` of the player's capsule, at its place and 0.6 m lower, with max separation `reach`, on a press of jump when not riding.
- Sets the collision group of the ridden animal's collider; clears it 0.6 s after getting off.
- On a jump off: `SetLinearVelocity` of the player (the saddle's velocity plus `hop` m/s to the animal's left and 1 m/s up).

## Order and dependencies

After the input systems and `LookTurn`; before `MountSteering` (needs the `AnimalSteer`), `SteeredWalk`, `PlayerLegs` (reads `limp`, `jump_locked`) and `RiderSeat`. Needs `PlayerLegs` to have given the player its collision group and `Mountables` to have made the colliders (a frame earlier).

## Used in scenes

<!-- generated -->
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`

## Tests

`MOUNT.2` (gets on next to an animal, a jump away from one), `MOUNT.4` (gets off beside it, bumped into again), `MOUNT.5` (the animal goes away under its rider) in [`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp).
