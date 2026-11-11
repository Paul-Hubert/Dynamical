# SphereViewSys

> **Kind:** system · **Registry name:** `SphereView` · **Scope:** Host · **Lane:** Pre · **Needs renderer:** yes
> **Source:** [`src/view/sphere_view.h`](../../../src/view/sphere_view.h), [`src/view/sphere_view.cpp`](../../../src/view/sphere_view.cpp)

## Purpose
Viewer for the sphere roller task (`sphere_view`, `vr_roller` scenes): follows one world's `SphereAgent` sphere with the desktop camera, draws its target velocity (green bar) and horizontal velocity (orange bar) above it, marks its rotation with three white dots on its surface, and shows an ImGui panel "Sphere roller" (policy, target, speed, error, reward, decision count).

## Lifecycle
- **Construction / params:** `SphereViewSys(reg, params)`, all read with `params.value(key, default)` (wrong types throw a nlohmann exception, unknown keys ignored):
  - `world` (int, 0): world index (clamped into range each tick).
  - `distance` (float, 5.0): camera distance behind the sphere, m.
  - `height` (float, 2.6): camera height above the sphere centre, m.
  - `side` (float, 0.5): radians the camera swings around Y away from straight behind.
  - `smoothing` (float, 1.5): 1/s, exponential smoothing of the viewing direction.
  - `arrow_scale` (float, 0.6): bar metres per m/s.
  - `camera` (bool, true): false leaves the desktop camera alone (another system owns it) and hides the Follow checkbox.
  - `bars` (bool, true): draws the bars (panel checkbox).
  - `follow` starts equal to `camera`.
- **preinit / init:** None (default).
- **tick(dt):**
  1. Return if there are no `Worlds` or none; clamp `world`; return if the world has no entity with `SphereAgent`, `Transform`, `RigidBody` (first one is used).
  2. Pose: `Transform` position / rotation, replaced by `PhysicsPose` interpolated with `WorldStepperState::alpha` (lerp / slerp; alpha 1 when absent). Linear velocity from the world's `PhysicsWorld` when the body is created; `horizontal = (v.x, 0, v.z)`; `target = agent.targetVelocity()`; `r = half_extent.x`.
  3. If `follow`: wanted direction = normalized horizontal velocity when faster than 0.5 m/s, else `headingDirection(agent.heading)`; first tick snaps, later ticks mix with `k = 1 - exp(-smoothing * dt)` and renormalize. `look = position + (0, 0.3, 0)`; `eye = position + rotY(side) * -direction * distance + (0, height, 0)`; if `PhysicsWorld::castStatic(look, eye)` hits, `eye = hit + normal * 0.3`; if `groundBelow(eye + 3 up, 3.5)` hits, `eye.y = max(eye.y, ground + 0.5)`. Writes `Camera` position, pitch and yaw to look at `look`.
  4. Bars from `position + (0, r + 0.15, 0)`: target bar (lift 0) and velocity bar (lift 0.08), each a box part centred at half its length, oriented along it, scale `(0.09, 0.09, length)`, or 0 when `bars` is off or the length is below 1 mm.
  5. Dots: three box parts at `position + rotation * (r on X, Y, Z)`, scale 0.1, or 0 when the `Camera` is within `r + 0.1` of the centre (player inside a large sphere).
  6. ImGui: collapses "Dynamical", "Worlds", "Frame time" on first use; window "Sphere roller" (first-use (10, 10), 55 % background, auto resize): with `DYNAMICAL_ONNX`, the world's `PolicySources` entry (error in red, or file and step); world index; target speed; speed; error `|horizontal - target|`; reward from the world's `AgentIO` if present; decisions (steered, or `decisions / max_decisions`); Follow (if `camera`) and Bars checkboxes; World slider when there are several worlds.
- **finish:** destroys the part entities that are still valid and resets them to `entt::null`.

## Expected behavior
- Part entities (5) are created lazily on first use, each with `Transform` and `ModelRenderable` (box primitive), and re-created if destroyed elsewhere; with no `ModelManager` nothing is drawn.
- With `camera: false` the desktop camera is never written; the dots still hide when the camera is inside the sphere.
- Switching the World slider changes the followed sphere from the next tick.
- The camera never ends up behind static geometry on the look ray and stays 0.5 m above ground found under it.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Worlds`](../components/Worlds.md) | host | read | tick | selected world |
| [`SphereAgent`](../components/SphereAgent.md) | world | read | tick | `targetVelocity()`, `heading`, `speed`, `decisions`, `max_decisions`, `steered` |
| [`Transform`](../components/Transform.md) | world | read | tick | sphere pose fallback |
| [`RigidBody`](../components/RigidBody.md) | world | read | tick | `half_extent.x`, `body`, `created()` |
| [`PhysicsPose`](../components/PhysicsPose.md) | world | read | tick | interpolated pose |
| [`PhysicsWorld`](../components/PhysicsWorld.md) | world | read | tick | linear velocity, `castStatic`, `groundBelow` |
| [`AgentIO`](../components/AgentIO.md) | world | read | tick | `reward` |
| [`WorldStepperState`](../components/WorldStepperState.md) | host | read | tick | `alpha` |
| [`Camera`](../components/Camera.md) | host | readwrite | tick | follow writes position/pitch/yaw; position read for the dots |
| [`ModelManager`](../components/ModelManager.md) | host | read | tick | `load(primitives/box.gltf)` when creating parts |
| [`Transform`](../components/Transform.md) | host | write +create | tick | part entities |
| [`ModelRenderable`](../components/ModelRenderable.md) | host | write +create | tick | part entities, `tint` every tick |
| [`PolicySources`](../components/PolicySources.md) | host | read | tick | only with `DYNAMICAL_ONNX` |
| [`Transform`](../components/Transform.md) | host | write +destroy | finish | destroys the part entities |

## Side effects
- Up to 5 host entities created (destroyed in finish).
- Jolt queries (`castStatic`, `groundBelow`, `GetLinearVelocity`) on the world's physics system from the main thread.
- ImGui window "Sphere roller"; collapses three other windows on first use.

## Ordering and dependencies
- After [WorldStepperSys](WorldStepperSys.md) (this frame's poses and alpha) and [UISys](UISys.md).
- When `camera` is true it should come after any other camera writer; in `vr_roller` it runs with `camera: false` because [RollerPlayerSys](RollerPlayerSys.md) DesktopRoller owns the camera.
- Reads world registries while no world is stepping (worlds step synchronously in WorldStepper).

## Related
[RollerPlayerSys](RollerPlayerSys.md), [WorldViewSys](WorldViewSys.md), [WorldStepperSys](WorldStepperSys.md), [`docs/physical-agents/sphere-roller.md`](../../physical-agents/sphere-roller.md), [folder `src/view/`](../folders/src-view.md).
