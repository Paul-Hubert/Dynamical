# Grab

`GrabSys` in [`src/logic/player/grab.cpp`](../../../src/logic/player/grab.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `VRHands`, before `BodyDrives` · Parameters: no

Unity's Grabber and Thrower on the VR hands: while a hand's grip is held, what it touches is locked to it where they touch by a Jolt fixed constraint (a body, or the world: the player then pulls itself along). When the grip is let go a dynamic body leaves with the player's velocity plus the controller's.

## Behaviour

- **preinit**: gets the `PhysicsWorld`, connects `on_destroy<RigidBody>`, adds `step` to `FixedSteps::before`.
  **tick** does nothing.
- **step(h)**: advances its own simulation clock. For every `Hand` + `Grabber`:
  - `grabbing = grip > grip_threshold`.
  - A grip already held is released when the grip is let go (thrown) or the hand was teleported (`teleports` changed:
    not thrown, what it held stays behind; with the grip still held the hand grabs again in the same step).
  - Grabbing and not `grabbed`: **grab**. Needs the hand's body and the player's group (`PlayerLegs::group`). Collides
    the hand's shape with the world, max separation `touch`, ignoring sensors and the player's own bodies except what
    the other hand holds; hits sorted deepest first. The first hit that is not the hand nor an `Ungrabbable` entity gets
    a `FixedConstraint` at the contact point (world space, priority 1000, 40 velocity and 8 position steps: the hand's
    10000 N motor must not tear it). `grabbed` and `object` set; a dynamic entity gets `Grabbed` (its side's hand)
    and its collision group (`held_left`, `held_right` or `held_both`) so that the hands holding it do not collide
    with it.
  - Release: removes the constraint, clears `grabbed` / `object`, the `Grabbed` side; with no hand left the component
    goes and the body's group is cleared `collision_delay` s later (unless it is grabbed or held again by then);
    thrown: velocity = player body velocity + play area rotation x controller velocity, times `velocity_multiplier`;
    angular velocity scaled by `angular_velocity_multiplier`.
- **on_destroy\<RigidBody\>**: a hand or a held object losing its body drops its grips (no throw).
- **finish**: disconnects, removes its step and every constraint.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit, step | contact queries of the hand's shape, fixed constraints added / removed, velocities of thrown bodies, collision groups |
| [FixedSteps](../components/FixedSteps.md) | context | readwrite+create | preinit, finish | adds its step to `before` in preinit (created when missing), removes it in finish |
| [Hand](../components/Hand.md) | component | read | step, release, regroup | grip, side, player, teleports, velocity (thrown with it) |
| [Grabber](../components/Grabber.md) | component | readwrite | step, grab, release, signal on_destroy<RigidBody> | thresholds read; grabbing, grabbed, object written |
| [Grabbed](../components/Grabbed.md) | component | readwrite+create+destroy | grab (get_or_emplace), release (remove), regroup, step | on a dynamic entity held: the hand of each side; removed when no hand holds it |
| [Held](../components/Held.md) | component | read | step | a body still held by a Holder keeps its collision group |
| [Ungrabbable](../components/Ungrabbable.md) | tag | read | grab | such entities are skipped |
| [PlayerLegs](../components/PlayerLegs.md) | component | read | grab, regroup (helper groupOf) | the player's collision group; no grab before the legs gave one |
| [VRBody](../components/VRBody.md) | component | read | release | play area rotation: the controller's velocity in world space for a throw |
| [RigidBody](../components/RigidBody.md) | component | read | step, grab, release (helpers playerBodyId, bodyEntity); signal on_destroy<RigidBody> | body ids, the entity of a touched body; a body destroyed drops the grips on it |
| [PlayerCollision](../components/PlayerCollision.md) | context | read+create | grab, regroup (helpers playerCollision, setPlayerGroup, clearPlayerGroup) | the filter of what may be grabbed; held bodies go into sub group held_left / held_right / held_both, cleared `collision_delay` s after they are let go |

## Side effects

- Connects `on_destroy<RigidBody>` in preinit (disconnected in finish): the grips of a destroyed hand or object are taken off without a throw.
- At each physics step: `CollideShape` of the hand's shape (max separation `touch`), `FixedConstraint` (priority 1000, 40 velocity / 8 position steps) added at the deepest contact; removed on release, teleport, finish.
- On a throw: `SetLinearAndAngularVelocity` of the released body.
- Sets and clears collision groups of grabbed bodies.

## Order and dependencies

After `VRHands` (the grip and the teleport count of this step) and `PlayerLegs` (the group); before `Physics`. `BodyDrives` may run before or after it in the step.

## Used in scenes

<!-- generated -->
- [`player_vr.scene.json`](../../../resources/scenes/player_vr.scene.json): `host.pre`

## Tests

`PLAYER.7` (grabs the box it touches while the grip is held, carries and throws it; an `Ungrabbable` table is not grabbed) in [`tests/unit/player.cpp`](../../../tests/unit/player.cpp).
