# Conventions

## Coordinate frames

There are three frames. The simulation uses only the first one.

| Frame | Handedness | Up | Units | Used by |
|---|---|---|---|---|
| **Sim frame** | numerically Unity's (left-handed) | +y | metres, kg, seconds, radians | Jolt, body file, pose tracks, observations, rewards |
| Map frame | right-handed | +z (`Position::getVec3()` = x, y, height) | 1 tile = 1 m | `MapManager`, `Position`, renderer |
| Jolt's nominal frame | right-handed | any (gravity vector decides) | SI | — |

### Why the sim keeps Unity's numbers unchanged

Unity hands its left-handed coordinates straight to PhysX, which is a right-handed engine, and it
works: rigid-body equations written in components (cross products, quaternion products, inertia
tensors) are the same formulas in either handedness. A left-handed scene fed into a right-handed solver
simulates the mirror image of the scene, read back in the same mirrored coordinates. So:

- Positions, linear velocities, quaternions (x, y, z, w), angular velocities and torques from Unity go
  into Jolt **as is**. No sign flips, no axis swaps.
- Observations computed from Jolt state are then numerically comparable with Unity's on the same pose.
  This is what makes the M3 golden parity tests possible.
- Gravity is `(0, -9.81, 0)` (Unity plan D6).
- Joint frames and limits from Unity's `ConfigurableJoint` (or whatever the Unity body uses) are taken
  numerically too. The only thing to watch is code that *interprets* a rotation direction (e.g.
  "positive twist bends the knee forward"): that interpretation must come from the Unity numbers, never
  from intuition about a right-handed frame.

### Sim → map conversion (rendering and game integration only)

```cpp
// src/agent/frames.h
inline glm::vec3 simToMap(glm::vec3 p, glm::vec2 origin_xy, float origin_z) {
    return { origin_xy.x + p.x, origin_xy.y + p.z, origin_z + p.y };   // swap y and z
}
inline glm::quat simToMap(glm::quat q) {
    // R' = M R M with M = swap(y,z), det(M) = -1: the axis maps to -M*axis, the angle is kept.
    // (w, x, y, z) -> (w, -x, -z, -y); glm::quat takes (w, x, y, z).
    return glm::quat(q.w, -q.x, -q.z, -q.y);
}
```

Swapping y and z is a reflection (determinant −1), which is exactly the left→right handedness change,
so a sim pose drawn through `simToMap` looks correct in the right-handed z-up map. Write a unit test
that converts a segment's local axes and checks `cross(x', y') == z'` in the map frame.

## Units

- SI everywhere in `src/physics` and `src/agent`. Angles in radians internally; the body file may store
  degrees (as Unity does) and converts on load — the field name says which (`*_deg`).
- **1 map tile = 1 m.** Today a human is drawn as a sphere 1 tile wide, so this is consistent.
  Tile `level` (height) units must be confirmed in `MapGenerator` before terrain heightfields are used
  (see [physics.md](physics.md#ground)).

## Time

| Name | Meaning | Where it lives |
|---|---|---|
| `h` | Physics tick, seconds (e.g. 1/120) | `ScenarioDef::physics_hz` → `ArenaClock::h` |
| `decision_period` | Physics ticks per agent decision | `ScenarioDef::decision_period` |
| `tick` | Physics ticks since arena creation (`uint64_t`) | `ArenaClock::tick` |
| `episode_step` | Decisions since episode start | `Episode::step` |
| `max_episode_steps` | Decisions before `max_step_reached` | `ScenarioDef` |
| Game `Time::dt` | Game seconds per frame — **compressed ~600×**, unrelated to physics time | existing `Time` ctx |

Never feed the game's `Time::dt` into physics. See [game-integration.md](game-integration.md#the-time-scale-problem).

Unity counts `MaxStep` in academy steps (physics ticks) for agents, not decisions. Whichever the
Unity plan uses, store it under that meaning and name the field accordingly
(`max_steps_physics` or `max_steps_decisions`); never convert silently.

## Identifiers

| Id | Type | Rule |
|---|---|---|
| `entt::entity` | EnTT | Internal only. Recycled (version bits change). Never sent over the wire |
| `AgentId::value` | `int32_t` | Process-unique, assigned once when an agent slot is created, kept across episodes. Sent as `AgentInfoProto.id`. Allocated from a process-wide atomic counter starting at 0 |
| Behaviour name | `std::string` | `"<Name>?team=<n>"`, e.g. `Walker?team=0`. Fixed per agent slot |
| `group_id` | `int32_t` | 0 unless the scenario uses multi-agent groups (duel line) |
| Segment index | `uint16_t` | Index into `BodyDef::segments`; stable, used in observations and logs |

## Naming and code style

- Follow the existing style: `PascalCase` types, `snake_case` members, systems end in `Sys`, components
  are plain structs/classes in `components/`, `reg.ctx<T>()` / `reg.set<T>()` (EnTT 3.8 API — it is the
  vendored version in `lib/entt`, not a submodule).
- New systems use the existing `System` base and `DEFINE_SYSTEM`-style declarations, collected in
  `src/physics/systems/physics_system_list.h` and `src/agent/systems/agent_system_list.h`.
- `OPTICK_EVENT()` at the top of every `tick`.
- Definitions loaded from disk (`BodyDef`, `ScenarioDef`) are immutable after load and shared by
  pointer; per-arena mutable state goes in that arena's registry.
