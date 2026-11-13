# VRHands

`VRHandsSys` in [`src/logic/player/vr_player.cpp`](../../../src/logic/player/vr_player.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `PlayerLegs`, before `Grab` and `BodyDrives` · Parameters: no

Unity's Hand: each VR hand is a rigid body driven to its controller through a `BodyDrive` anchored on the player. This system writes the drive's target from the last trusted controller state, teleports a hand that is more than `failsafe` away, and gives the hand its share of the legs' forces so that standing does not leave the hands behind.

## Behaviour

- **preinit**: gets the `PhysicsWorld`; adds `step` to `FixedSteps::before`. **tick** does nothing.
- **step(h)**, for every `Hand` + `BodyDrive` + `Transform` whose player has `VRBody`, `VRTracking`, `PlayerLegs` and
  both bodies exist:
  - Once the player has a group: a hand without a group goes into it, sub group `left_hand` / `right_hand` (it does
    not collide with its body).
  - When focused, not `pose_hold` and the controller's pose is valid: `Hand` takes the controller state (position from
    the head, rotation, velocities, grip, trigger) and `tracked`.
  - `BodyDrive`: anchor = player; tracked: target = play area rotation x (position, rotation, velocity, angular
    velocity); else the rest pose, zero feed forward.
  - **Failsafe**: when the hand's body is more than `failsafe` from the player's position + target, it is put there with
    the player's velocity + the feed forward, its `PhysicsPose` set to the target, `teleports` incremented (`Grab` then
    lets go).
  - Adds `applied_force x hand mass / player mass` to the hand (Unity's ApplyForcesToHands).
- **finish**: removes its step.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit, step | hand bodies teleported by the failsafe, forces added, collision group set |
| [FixedSteps](../components/FixedSteps.md) | context | readwrite+create | preinit, finish | adds its step to `before` in preinit (created when missing), removes it in finish |
| [Hand](../components/Hand.md) | component | readwrite | step | player, side, failsafe, rest read; the last trusted controller state (tracked, position from the head, rotation, velocities, grip, trigger) written; teleports counted |
| [BodyDrive](../components/BodyDrive.md) | component | write | step | anchor = the player; position, rotation, velocity, angular_velocity = the controller from the head turned by the play area (or the rest pose) |
| [Transform](../components/Transform.md) | component | read | step | view filter only |
| [VRBody](../components/VRBody.md) | component | read | step | of the hand's player: pose_hold, head, play area rotation |
| [VRTracking](../components/VRTracking.md) | component | read | step | of the hand's player: focused, the controller's pose, velocities, grip, trigger |
| [PlayerLegs](../components/PlayerLegs.md) | component | read | step | of the hand's player: group, applied_force |
| [PhysicsPose](../components/PhysicsPose.md) | component | write | step | when the failsafe teleports a hand: both poses set to the target |
| [RigidBody](../components/RigidBody.md) | component | read | step (helper playerBodyId) | the body ids of the hand and of its player |
| [PlayerCollision](../components/PlayerCollision.md) | context | read+create | step (helper setPlayerGroup) | the filter: a hand without group goes into the player's group, sub group left_hand or right_hand |

## Side effects

- At each physics step: `SetPositionAndRotation` + `SetLinearAndAngularVelocity` of a hand more than `failsafe` from its target; `AddForce` of its share (by mass) of the legs' force.
- Sets the collision group of each hand body once.

## Order and dependencies

After `PlayerLegs` in the scene so that its step runs after the legs' (it reads this step's `applied_force` and the group), before `Grab` (reads `grip`, `teleports`) and `BodyDrives` (turns the target into motor velocities in the same step).

## Used in scenes

<!-- generated -->
- [`player_vr.scene.json`](../../../resources/scenes/player_vr.scene.json): `host.pre`

## Tests

`PLAYER.4` (a hand follows its controller), `PLAYER.5` (failsafe), `PLAYER.7`, `PLAYER.9` (a hand does not go through a wall) in [`tests/unit/player.cpp`](../../../tests/unit/player.cpp).
