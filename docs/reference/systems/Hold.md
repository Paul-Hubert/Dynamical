# Hold

`HoldSys` in [`src/logic/player/grab.cpp`](../../../src/logic/player/grab.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `PlayerLegs` (and `RiderSeat`), before `BodyDrives` · Parameters: no

The FPS player's hold (Unity's FPSPlayer and its Weapon joint): interact picks up the dynamic body looked at, which then follows a point in front of the eyes through a `BodyDrive` anchored on the player (it lags, collides and weighs on the player); interact again drops it, primary throws it, the control axis (wheel) brings it nearer or further.

## Behaviour

- **preinit**: gets the `PhysicsWorld`, adds `step` to `FixedSteps::before`.
- **tick** (decisions, once a frame), for every `Holder` + `PlayerView`: presses are changes from `was_interact` /
  `was_primary`. A held entity that lost its `RigidBody`, `BodyDrive` or `Held` is forgotten. `distance` +=
  control axis x `wheel_step`, clamped to [min, max]. Interact pressed: drop what is held, or **pick**. Primary pressed
  with something held: drop with velocity `forward x throw_speed` + the player's velocity.
- **pick**: needs the player's body and group. Ray of `reach` along the view from the eyes, ignoring the player's bodies
  and sensors. The hit must be a dynamic body of an entity without `Ungrabbable`, `Held`, `Grabbed` or `BodyDrive`.
  It gets a `BodyDrive` (anchor the player, target where it is, `max_force` / `max_torque` of the Holder) and `Held`;
  `held_rotation` keeps its rotation in the view's frame; its collision group becomes the player's `held_body`.
- **drop**: removes `BodyDrive` and `Held`, clears the collision group, sets the throw velocity if any.
- **step(h)**: for every holder with something: dropped when `driven` and the drive's `error` is over
  `break_distance` (stuck behind something); else target = `forward x distance`, rotation = view x `held_rotation`.
  The legs' forces are not passed on (what is held has to weigh on the drive).
- **finish**: removes its step.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [ScriptedInput](../components/ScriptedInput.md) | component | read | helper playerInput | answers for the player when present (tests, scripts) |
| [MouseKeyboardInput](../components/MouseKeyboardInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [ControllersInput](../components/ControllersInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [VRPlayerInput](../components/VRPlayerInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit, tick, step | ray cast from the eyes, velocities of dropped / thrown bodies, collision groups |
| [FixedSteps](../components/FixedSteps.md) | context | readwrite+create | preinit, finish | adds its step to `before` in preinit (created when missing), removes it in finish |
| [Holder](../components/Holder.md) | component | readwrite | tick, step, pick, drop | tuning read; distance changed by the wheel; held, held_rotation, was_interact, was_primary written |
| [PlayerView](../components/PlayerView.md) | component | read | tick, step, pick | forward and rotation: the ray, the throw, the drive's target |
| [BodyDrive](../components/BodyDrive.md) | component | readwrite+create+destroy | pick (emplace), step, drop (remove) | made on what is picked up (anchor the player, max_force / max_torque of the Holder); its target set every step; `driven` and `error` read to drop what is stuck |
| [Held](../components/Held.md) | component | readwrite+create+destroy | pick (emplace), drop (remove), tick, step | on the held body: `by` the player |
| [Ungrabbable](../components/Ungrabbable.md) | tag | read | pick | not picked up |
| [Grabbed](../components/Grabbed.md) | component | read | pick | what a hand holds is not picked up |
| [PlayerLegs](../components/PlayerLegs.md) | component | read | pick (helper groupOf) | the player's collision group |
| [RigidBody](../components/RigidBody.md) | component | read | tick, step, pick (helpers playerBodyId, bodyEntity) | body ids; the held entity must still have one |
| [PlayerCollision](../components/PlayerCollision.md) | context | read+create | pick, drop (helpers playerCollision, setPlayerGroup, clearPlayerGroup) | the ray ignores the player's bodies; the held body goes into sub group held_body, cleared when dropped |

## Side effects

- Narrow phase ray cast of `reach` along the view on a press of interact.
- Sets the collision group of the held body; clears it and, on a throw, sets its linear velocity when it is dropped.
- Adds and removes BodyDrive components (BodyDrives makes the constraint).

## Order and dependencies

After the input systems and `LookTurn`; before `BodyDrives` (which builds the motor for the `BodyDrive` it adds, and whose `error` it reads in the next step) and `Physics`.

## Used in scenes

<!-- generated -->
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.pre`
- [`player_fps.scene.json`](../../../resources/scenes/player_fps.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`

## Tests

`PLAYER.8` (picks up the box looked at, it follows the view, is thrown), `PLAYER.9` (too heavy stays on the ground) in [`tests/unit/player.cpp`](../../../tests/unit/player.cpp).
