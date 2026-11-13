# MountSteering

`MountSteeringSys` in [`src/logic/player/mount.cpp`](../../../src/logic/player/mount.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `Mounting`, before `SteeredWalk` · Parameters: no

A rider's walk axis, laid along the horizontal of its view as the legs would walk it, is the heading asked of the animal; its length the speed: the `Mountable`'s walk speed, or its run speed with the run button. The animal knows only its `AnimalSteer`.

## Behaviour

- **tick**, for every `Rider` + `PlayerView` riding an animal with `AnimalSteer`, `Mountable` and `Transform`:
  - `amount = min(|movement axis|, 1)`; under 0.1: `speed = 0`, the heading stays the last one asked.
  - Else `direction = move.x x right + move.y x forward` of the view's yaw, `AnimalSteer::yaw = atan2(-x, -z)`,
    `speed = (run ? max(run_speed, walk_speed) : walk_speed) x Transform::scale.y x amount`, `run` = the run button.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [ScriptedInput](../components/ScriptedInput.md) | component | read | helper playerInput | answers for the player when present (tests, scripts) |
| [MouseKeyboardInput](../components/MouseKeyboardInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [ControllersInput](../components/ControllersInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [VRPlayerInput](../components/VRPlayerInput.md) | component | read | helper playerInput | a device of the player: the active one used last answers |
| [Rider](../components/Rider.md) | component | read | tick | the animal ridden |
| [PlayerView](../components/PlayerView.md) | component | read | tick | yaw: the frame of the walk axis |
| [AnimalSteer](../components/AnimalSteer.md) | component | write | tick | of the animal: yaw, speed, run (speed 0 and heading kept when the axis is under 0.1) |
| [Mountable](../components/Mountable.md) | component | read | tick | walk_speed, run_speed |
| [Transform](../components/Transform.md) | component | read | tick | the animal's scale (speeds are the animal's own) |

## Side effects

- None.

## Order and dependencies

After `Mounting` (which emplaces the `AnimalSteer`) and `LookTurn` (the view); before `SteeredWalk` / `AnimalBotSteer`, which turn the `AnimalSteer` into a walk.

## Used in scenes

<!-- generated -->

## Tests

`MOUNT.3` (the walk axis along the view steers the animal, run asks for its run) in [`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp).
