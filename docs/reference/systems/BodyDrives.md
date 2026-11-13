# BodyDrives

`BodyDrivesSys` in [`src/logic/player/body_drive.cpp`](../../../src/logic/player/body_drive.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `VRHands`, `Grab`, `Hold`, before `RigidBody` and `Physics` · Parameters: no

The physics of every `BodyDrive` (Unity's ConfigurableJoint drives as the hand uses them): a Jolt `SixDOFConstraint` between the anchor body (or the world) and the driven body, every axis free, with velocity motors whose force and torque are limited per axis. Each step the motors ask for `feed forward x max(1 - |error|, 0) + gain x error / h`; the anchor gets the reaction. VR hands and the FPS player's held body are both this.

## Behaviour

- **preinit**: gets the `PhysicsWorld`; connects `on_destroy<BodyDrive>` (drops the motor) and `on_destroy<RigidBody>`
  (drops the body's motor and every motor anchored on it); adds `step` to `FixedSteps::before`. **tick** does nothing.
- **step(h)**, for every `BodyDrive` + `RigidBody`:
  - Ready = the body exists, is `Dynamic`, and the anchor (if any) has a body. A motor whose drive is not ready, or
    whose anchor or `rotate` changed, is dropped (`driven = false`).
  - New motor: frames at the anchor's origin (its centre of mass offset taken out) and the body's centre of mass, every
    axis made free, translation motors (and rotation motors with `rotate`) in velocity mode, `AddConstraint`.
  - Force limits `max_force`, torque limits `max_torque`.
  - `error` = anchor origin + anchor rotation x `position` - centre of mass; linear target velocity as above, in the
    anchor's frame. With `rotate`: the shortest rotation to the target gives `angular_error` and an angular target
    velocity (in the body's frame).
  - `driven = true`; the body and its anchor are activated.
  - Motors whose entity lost `BodyDrive` or `RigidBody` are dropped.
- **finish**: disconnects the signals, removes its step and every constraint.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite+create | preinit, step, finish, signal handlers | constraints added / removed, motor targets, bodies activated |
| [FixedSteps](../components/FixedSteps.md) | context | readwrite+create | preinit, finish | adds its step to `before` in preinit (created when missing), removes it in finish |
| [BodyDrive](../components/BodyDrive.md) | component | readwrite | step, drop; signal on_destroy<BodyDrive> | target, gains, limits, anchor, rotate read; error, angular_error, driven written (driven false when the motor is dropped) |
| [RigidBody](../components/RigidBody.md) | component | read | step (helper playerBodyId); signal on_destroy<RigidBody> | the body ids of the driven body and the anchor, its motion (only dynamic bodies are driven); a destroyed body drops its motor and those anchored on it |

## Side effects

- Connects `on_destroy<BodyDrive>` and `on_destroy<RigidBody>` in preinit, disconnects them in finish.
- Creates a `SixDOFConstraint` (all axes free, motors in velocity mode) per ready drive, `AddConstraint`; `RemoveConstraint` when the drive, its body or its anchor goes, the anchor or `rotate` changes, or in finish.
- Every physics step: force / torque limits and target velocities of the motors, `ActivateBody` of the body and its anchor.

## Order and dependencies

After the systems that write the drives' targets in their own steps (`VRHands`, `Hold`) so that the motor targets of a step use that step's target; before `Physics`. Needs `RigidBody` to have made the bodies (from the frame after the components are added).

## Used in scenes

<!-- generated -->
- [`player_deer.scene.json`](../../../resources/scenes/player_deer.scene.json): `host.pre`
- [`player_fps.scene.json`](../../../resources/scenes/player_fps.scene.json): `host.pre`
- [`player_horse.scene.json`](../../../resources/scenes/player_horse.scene.json): `host.pre`
- [`player_vr.scene.json`](../../../resources/scenes/player_vr.scene.json): `host.pre`

## Tests

`PLAYER.4` (a hand follows its controller in position and rotation: the motors' signs), `PLAYER.5`, `PLAYER.7` to `PLAYER.9` in [`tests/unit/player.cpp`](../../../tests/unit/player.cpp); `MOUNT.2` to `MOUNT.5` run it.
