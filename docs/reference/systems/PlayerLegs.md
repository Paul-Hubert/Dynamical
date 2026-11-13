# PlayerLegs

`PlayerLegsSys` in [`src/logic/player/player_body.cpp`](../../../src/logic/player/player_body.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `LookTurn` / `VRBody` / `Mounting`, before `RigidBody`, `TerrainColliders` and `Physics` · Parameters: no

Unity's GenericPlayer: there are no leg bodies. A ray from the eyes finds the ground, a vertical force asks for the speed that brings the eyes to `height` over it, a horizontal force asks for the walk (or sprint) speed along the view's heading, a vertical force jumps. Forces are Unity's: (gain x speed error) x mass / 10. It also gives each player its collision group, which all the other player systems rely on.

## Behaviour

- **preinit**: gets the `PhysicsWorld`; adds `step` to `FixedSteps::before`.
- **tick** (once a frame): for every `PlayerLegs` + `Transform`, appends to `TerrainNeeds` (created when missing) the box
  from `position - (1.5, height + reach + 1.5, 1.5)` to `position + (1.5, 0, 1.5)`: `TerrainColliders` meshes the
  ground under the legs, which the capsule (the chest) does not reach.
- **step(h)** (before each 60 Hz Jolt step), for every `PlayerLegs` + `PlayerView` + `RigidBody`:
  - `jump_locked` is cleared once jump is let go. Skips players whose body is not made yet.
  - First time: takes `PlayerCollision::next_group` as `group` and puts the body in sub group `body`.
  - Resets `applied_force`, `standing`, `walking`, `ground`, `ground_velocity`; stops there when `limp`.
  - **Stand**: ray from the eyes down `max(height, 0.3) + reach`, ignoring sensors and the player's own bodies. On a
    hit: `standing`, `ground_distance`, `ground` (the entity of the body, if a RigidBody made it), `ground_velocity`
    (point velocity of a non static ground). Force `stand_force x (stand_speed x 2 x min(0.5, height - distance) +
    ground_velocity.y - velocity.y)`, capped at `leg_force`, applied when positive, x mass / 10.
  - **Walk**: the movement axis on the heading of `PlayerView::yaw` (length at most 1); the velocity relative to the
    ground, without the part along the axis when it goes the other way (no braking for going faster); force
    `walk_force x (axis x (run ? sprint_speed : walk_speed) - relative)`, x `air_walk` off the ground, horizontal only.
    `walking` = an axis and standing.
  - **Jump**: standing, jump held and not `jump_locked`: `min(jump_force x (jump_speed + ground_velocity.y - velocity.y),
    leg_force)` up. (Held jump keeps pushing while standing; the ground is left within a step or two.)
  - Every force is added to `applied_force`.
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
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit, step | ray casts under the eyes, forces on the player's body, collision group of the body |
| [FixedSteps](../components/FixedSteps.md) | context | readwrite+create | preinit, finish | adds its step to `before` in preinit (created when missing), removes it in finish |
| [TerrainNeeds](../components/TerrainNeeds.md) | context | readwrite+create | tick | created when missing; appends a box per player from 1.5 m around the eyes down to height + reach + 1.5 m below, so TerrainColliders meshes the ground under the legs |
| [PlayerLegs](../components/PlayerLegs.md) | component | readwrite | tick, step | tuning and `limp` / `jump_locked` read; standing, walking, ground_distance, ground, ground_velocity, applied_force, group written; jump_locked cleared when jump is let go |
| [PlayerView](../components/PlayerView.md) | component | read | step | yaw: the heading of the walk |
| [Transform](../components/Transform.md) | component | read | tick | eyes position for the terrain box |
| [RigidBody](../components/RigidBody.md) | component | read | step (helpers playerBodyId, bodyEntity) | the player's body id; the entity of the body stood on |
| [PlayerCollision](../components/PlayerCollision.md) | context | readwrite+create | step (helpers playerCollision, setPlayerGroup) | created when missing; `next_group` taken for a player without a group, its body put in sub group `body`; the filter excludes the player's own bodies from the ground ray |

## Side effects

- At each 60 Hz physics step (FixedSteps.before): a narrow phase ray cast per player, `AddForce` of the stand, walk and jump forces, `ActivateBody`.
- Sets the collision group of the player's body once (`SetCollisionGroup`).

## Order and dependencies

After the systems that set `height` (`VRBody`), `limp` / `jump_locked` (`Mounting`) and the view (`LookTurn`, `VRBody`); before `VRHands` (which carries `applied_force` to the hands and needs `group`), `Grab`, `Hold`, `Mounting` of the next frame (they need `group`), and before `TerrainColliders` (reads `TerrainNeeds`) and `Physics` (calls the step). Its step runs in the order the systems registered: before `RiderSeat`, `VRHands`, `Grab`, `Hold`, `BodyDrives`.

## Used in scenes

<!-- generated -->
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.pre`
- [`player_fps.scene.json`](../../../resources/scenes/player_fps.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`
- [`player_vr.scene.json`](../../../resources/scenes/player_vr.scene.json): `host.pre`

## Tests

`PLAYER.1` (stands at its height), `PLAYER.2` (walk, sprint, stop), `PLAYER.3` (jump) in [`tests/unit/player.cpp`](../../../tests/unit/player.cpp); `MOUNT.2` to `MOUNT.5` (limp while riding, standing after) in [`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp).
