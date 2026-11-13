# RiderSeat

`RiderSeatSys` in [`src/logic/player/mount.cpp`](../../../src/logic/player/mount.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `PlayerLegs`, before `Hold`, `BodyDrives` and `Physics` · Parameters: no

Keeps a riding player's body over the saddle with a spring-damper acceleration toward the saddle's place and velocity, plus gravity cancelled, faded in over `Rider::settle` and capped at `max_acceleration`: the body's acceleration has no jump, getting on or riding.

## Behaviour

- **preinit**: gets the `PhysicsWorld`, adds `step` to `FixedSteps::before`.
- **tick** (once a frame), for every `Rider`: not riding a valid animal with `Mountable` and `Transform`:
  `seat_known = false`. Else `seat = saddleOf(animal)` (the joint's world matrix from `AnimalPose` when the
  `Mountable` has one, else the Transform); the frame's velocity (discarded above 60 m/s) eases `seat_velocity` with a
  time constant of 0.12 s (reset to 0 the first frame); `seat_position = seat`, `seat_known = true`.
- **step(h)**: for every riding `Rider` with `seat_known` and a dynamic body: `blend += h / settle` (to 1); target =
  `seat_position + (0, seat, 0)`; `pull = (k2 (target - eyes) + 2 k (seat_velocity - velocity)) x smoothstep(blend)`,
  k = `stiffness`, length capped at `max_acceleration`; `AddForce((pull - gravity) x mass)`, body activated.
- **finish**: removes its step.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit, step | gravity read, forces on the rider's body |
| [FixedSteps](../components/FixedSteps.md) | context | readwrite+create | preinit, finish | adds its step to `before` in preinit (created when missing), removes it in finish |
| [Rider](../components/Rider.md) | component | readwrite | tick, step | seat_position, seat_velocity (smoothed), seat_known written each frame; blend raised each step; seat, stiffness, max_acceleration, settle read |
| [Mountable](../components/Mountable.md) | component | read | tick (and helper saddleOf) | the saddle |
| [Transform](../components/Transform.md) | component | read | tick (helper saddleOf) | the animal's pose |
| [AnimalPose](../components/AnimalPose.md) | component | read | tick (helper saddleOf) | world matrix of the joint that carries the saddle |
| [RigidBody](../components/RigidBody.md) | component | read | step (helper playerBodyId) | the rider's body id |

## Side effects

- At each physics step: `AddForce` on the rider's body ((pull - gravity) x mass) and `ActivateBody`.

## Order and dependencies

After `AnimalAnimation` and `AnimalGait` (the saddle of this frame) and after `Mounting`; before `Physics`. `Mounting` reads `blend` and `seat_velocity` the next frame.

## Used in scenes

<!-- generated -->
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`

## Tests

`MOUNT.2` (carried up to the saddle without a jolt), `MOUNT.3` (stays on the saddle) in [`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp).
