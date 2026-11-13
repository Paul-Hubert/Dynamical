# SphereView

`SphereViewSys` in [`src/view/sphere_view.cpp`](../../../src/view/sphere_view.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: yes

The view of the sphere roller scenes (docs/physical-agents/sphere-roller.md): a camera that follows one world's sphere
from behind its motion, two bars from the top of the sphere (green: target velocity, orange: horizontal velocity),
three white dots that turn with it, and the "Sphere roller" panel (policy, target, speed, error, reward, decisions).

## Behaviour

**tick** (nothing without `Worlds` or worlds, or when the world has no entity with `SphereAgent`, `Transform`,
`RigidBody`):

1. `world` is clamped to the worlds; the first sphere of it.
2. Pose: the `PhysicsPose` mixed with `WorldStepperState::alpha` (as WorldView draws it), else the `Transform`.
   Velocity from Jolt (`PhysicsWorld::bodies().GetLinearVelocity`) once the body exists.
3. Follow camera (`follow`, which starts as `camera`): the wanted direction is the horizontal velocity above 0.5 m/s,
   else the agent's heading; smoothed with rate `smoothing`; the eye is `distance` behind (swung `side` radians) and
   `height` up, pulled in front of static geometry in the way (`castStatic`) and kept 0.5 m over the ground
   (`groundBelow`); looks at the sphere + 0.3 m.
4. Markers on the host (made on first use, `./resources/models/primitives/box.gltf`): two 9 cm bars `arrow_scale` m
   per m/s, 15 cm above the sphere (hidden with `bars` false or a zero length); three 10 cm dots at the surface along
   the sphere's axes, hidden when the camera is inside the sphere.
5. Collapses the "Dynamical", "Worlds" and "Frame time" windows on first use; "Sphere roller" window: with ONNX, the
   policy file and step of the world (`PolicySources`) or its error; world, target, speed, error, reward
   (`AgentIO`), decisions; "Follow" (when `camera`), "Bars", and a world slider when there are several.

**finish**: destroys the five marker entities.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `world` | int | 0 | world whose sphere is shown (panel slider too) |
| `distance` | float | 5.0 | camera distance behind, m |
| `height` | float | 2.6 | camera height over the sphere, m |
| `side` | float | 0.5 | radians the camera swings to the side of the motion |
| `smoothing` | float | 1.5 | 1/s, smoothing of the viewing direction |
| `arrow_scale` | float | 0.6 | metres of bar per m/s |
| `camera` | bool | true | false: leaves the desktop camera alone (another system owns it, vr_roller), no Follow box |
| `bars` | bool | true | draw the two bars |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Worlds](../components/Worlds.md) | context | read | tick | the world asked for (clamped to the count) |
| [SphereAgent](../components/SphereAgent.md) | component | read | tick (world registry) | heading, targetVelocity(), speed, decisions, steered |
| [RigidBody](../components/RigidBody.md) | component | read | tick (world registry) | body id (velocity) and half_extent.x (radius) |
| [Transform](../components/Transform.md) | component | read | tick (world registry) | position and rotation when there is no PhysicsPose |
| [PhysicsPose](../components/PhysicsPose.md) | component | read | tick (world registry) | prev / current pose mixed with the stepper alpha |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | read | tick (world registry) | linear velocity of the body, castStatic / groundBelow to keep the camera out of the terrain |
| [AgentIO](../components/AgentIO.md) | context | read | tick (world registry) | reward shown |
| [WorldStepperState](../components/WorldStepperState.md) | context | read | tick | alpha: drawn where WorldView draws it |
| [Camera](../components/Camera.md) | context | readwrite | tick | follow camera writes position, yaw, pitch; read to hide the dots from a camera inside the sphere |
| [PolicySources](../components/PolicySources.md) | context | read | tick | policy file, step or error of the world (built with DYNAMICAL_ONNX) |
| [ModelManager](../components/ModelManager.md) | context | readwrite | tick (part) | load("./resources/models/primitives/box.gltf") |
| [ModelC](../components/ModelC.md) | component | write+create | tick (part, ModelManager::load) | the primitive box, on its first load |
| [Transform](../components/Transform.md) | component | readwrite+create+destroy | tick (part, place), finish | five host marker entities (2 bars, 3 dots) made when missing, posed every tick, destroyed in finish |
| [ModelRenderable](../components/ModelRenderable.md) | component | readwrite+create+destroy | tick (part, place), finish | the markers' box model, tint set every tick |

## Side effects

- ImGui window "Sphere roller"; collapses "Dynamical", "Worlds" and "Frame time" on first use.
- Five host entities for the markers, destroyed in finish.

## Order and dependencies

After WorldStepper (this frame's ticks and alpha) and PlayerControl. In `vr_roller` with `camera` false after the
rollers. Its markers are host entities drawn by ModelPass.

## Used in scenes

<!-- generated -->

## Tests

- `SR.18 vr_roller: ...` (`tests/onnx/vr_roller.cpp`: world 1, camera false in `vr_roller`). `tools/sphere_video.py`
  films it.
