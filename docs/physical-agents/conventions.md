# Conventions

## Frames

| Frame | Handedness | Up | Forward | Used by |
|---|---|---|---|---|
| **Sim** | left-handed (Unity's numbers) | +y | +z | Jolt worlds of agents, body file, pose tracks, observations, rewards, replays |
| **Engine** | right-handed | +y | −z | Renderer, `Transform`, marching-cubes terrain, `base` scene bodies |

**Why the sim keeps Unity's numbers (UP §3.1).**
- Unity hands left-handed coordinates to PhysX unchanged, and rigid-body physics is mirror-symmetric.
- So Jolt can take Unity's positions, Hamilton quaternions (x, y, z, w), velocities, torques, joint
  axes and limits verbatim.
- That keeps every observation bit-comparable for the M3 golden tests.
- No code in `src/physics` or `src/agent` converts frames.

**Sim → engine (drawing only).** Mirror z:

```cpp
// src/agent/frames.h
inline glm::vec3 simToEngine(glm::vec3 p) { return { p.x, p.y, -p.z }; }
// R' = M R M with M = diag(1, 1, -1): the rotation axis maps to -M·axis, the angle is kept.
inline glm::quat simToEngine(glm::quat q) { return glm::quat(q.w, -q.x, -q.y, q.z); }   // glm: (w, x, y, z)
```

- The sim's forward (+z) then draws as the engine's forward (−z), so a walker heading "forward" in Unity
  walks into the screen.
- `WorldView` applies this, plus a per-world offset. A unit test converts a segment's local axes and
  checks that `cross(x', y') == z'` holds in the engine frame.
- Agents placed in `base` (M9) use the inverse conversion when they read engine terrain.

## Unity conventions to reproduce exactly

These matter wherever ported code builds or decomposes rotations (UP §3.1):

| Unity | Meaning | C++ |
|---|---|---|
| `Quaternion.Euler(x, y, z)` | applies Z, then X, then Y (degrees) | `unityEuler(glm::vec3 deg)` in `src/agent/unity_math.h` |
| `q.eulerAngles` | decomposes in the same order into [0, 360) | `unityEulerAngles(q)` |
| `Transform.forward / up / right` | +z / +y / +x rotated by the rotation | `unityForward(q)` … |
| `Vector3.Angle`, `Quaternion.Angle`, `SignedAngle` | degrees, clamped dot | same names, unit-tested against values logged in Unity |

Every helper in `unity_math.h` has a test against numbers logged from Unity.

## Units

- SI throughout the sim: metres, kilograms, seconds, radians internally.
- Files may store degrees as Unity does; the field name says so (`*_deg`), and loaders convert.
- The engine's marching-cubes world is 4 units per cube. One engine unit = 1 m. The walker drawn in
  `walker_view` uses 1 sim m = 1 engine unit.

## Time

| Name | Meaning | Source |
|---|---|---|
| `h` | Physics tick in seconds. UP §3.1 gives Unity's exact value (a rational close to 1/60) and the `physicsRate` override | world `clock.hz` or `clock.dt` |
| `decision_period` | Ticks per decision | agent parameters |
| `tick` | Ticks since world creation (`uint64_t`) | `WorldClock` |
| `episode_tick` | Ticks since episode start; decision phase counts from here (UP §3.6) | `Episode` |
| `max_frames` | Truncation limit in **ticks**, scaled by physics rate (UP §3.7) | agent parameters |
| frame `dt` | Wall-clock frame time in `dynamical` | `Game` loop; never fed to agent physics |

Name every field after its unit (`*_ticks`, `*_decisions`, `*_s`); never convert silently.
`arcFrames` is **not** scaled by the physics rate (UP §3.7); keep that quirk named and tested.

## Identifiers

| Id | Rule |
|---|---|
| `entt::entity` | Internal only; recycled. Never on the wire or in files |
| `AgentId` (`int32_t`) | Process-unique, assigned once per agent slot from an atomic counter, kept across episodes. Sent as `AgentInfoProto.id` |
| Behaviour name | Exact Unity names (`HeidelBotBehavior`, `AnySpeedBotBehavior`, UP §3.1), plus `?team=N` |
| Segment index / joint index | Order of the body file = Unity's joint order (UP §3.4). Never re-sorted |
| Layout version | `"<body>_<n>"`, e.g. `heidelbot_notoes_1`. Bumped on any change to observation or action content |

## Code style

- Match master: `PascalCase` types, `snake_case` members, systems end in `Sys`, plain-struct components,
  EnTT 3.16 ctx API (`reg.ctx().emplace/get/find`).
- `OPTICK_EVENT()` at the top of every `tick`.
- Definitions loaded from disk (`BodyDef`, `PoseTrack`, `AgentParams`) are immutable after load and
  shared by `std::shared_ptr<const T>`. Mutable per-world state lives in that world's registry.
- No `rand()` in sim code; use the world RNG.
