# Conventions

## One frame: the engine's

**The simulation is native to the engine**: right-handed, +y up, −z forward, metres. Jolt worlds of
agents, the body file in memory, observations, rewards, replays and the renderer all share it.

Unity's left-handed coordinates exist in exactly one place: the **Unity importer**
(`src/agent/unity_import.*`). It reads the files the Unity exporter writes verbatim and converts them
once at load.

| Quantity (Unity, left-handed) | Engine (right-handed) |
|---|---|
| point / vector `(x, y, z)` | `(x, y, −z)` |
| quaternion `(x, y, z, w)` | `(−x, −y, z, w)` |
| angular velocity, torque (axial vectors) | `(−x, −y, z)` |
| joint `axis`, `secondaryAxis` | as vectors; the importer then derives the constraint frame in the engine's handedness |
| twist and swing limits | kept. Each twist limit is checked against the probes, because reflection can swap low and high |
| Unity forward (+z) | engine forward (−z) |

- **Why mirroring z is safe.** Rigid-body physics is mirror-symmetric, and mirroring z maps Unity's
  forward onto the engine's forward. The walker therefore looks and moves the same, with the left leg
  still on the left.
- **What it buys.** There is no conversion when drawing, policy walkers can be placed in `base`, and
  all C++ is written in one handedness.
- **No Unity math in the runtime.** `Quaternion.Euler` ZXY, `eulerAngles` and Unity's
  `Referential` classes are not reproduced in `src/agent`. The task is defined in its own terms
  ([agent-runtime.md](agent-runtime.md)).
- **Unity math stays in two places.** `src/agent/unity_math.h` (ZXY Euler and friends) serves only
  the importer and the oracle tests that compare against Unity logs. The oracle tests convert Unity
  golden data with the same importer.
- **Importer test.** It converts a segment's local axes and checks `cross(x', y') == z'`. It also
  imports the rest pose and checks that every joint anchor coincides with its parent's.

## Units

- SI throughout: metres, kilograms, seconds, radians.
- Files from Unity store degrees, and their field names say so (`*_deg`). The importer converts them.
- One engine unit is 1 m, both in the marching-cubes world and in agent worlds.

## Time

| Name | Meaning | Source |
|---|---|---|
| `h` | Physics tick. Default 1/60 s, as Unity; 120, 240 and 480 Hz are allowed | params `physics_hz` |
| `decision_period` | Ticks per decision. **30 Hz control** is the invariant: 2 at 60 Hz, 4 at 120 Hz | derived: `physics_hz / control_hz` |
| `episode_ticks` | Truncation length; **33.3 s** is the invariant (2000 ticks at 60 Hz) | derived from `episode_s` |
| `arc_ticks` | Target re-placement period. Unity does not scale `arcFrames` with the physics rate; here it is defined in seconds (`arc_s` = 6.67 s) | params |
| frame `dt` | Wall-clock frame time in `dynamical`; never fed to agent physics | `Game` |

Rates and durations are stored in seconds and Hz, and tick counts are derived from them. Unity's
quirk of scaling some counts and not others is not reproduced; this is recorded in
[unity-analysis.md](unity-analysis.md) §8.

## Identifiers

| Id | Rule |
|---|---|
| `entt::entity` | Internal only; recycled. Never on the wire or in files |
| `AgentId` (`int32_t`) | Process-unique, assigned once per agent slot from an atomic counter, kept across episodes. Sent as `AgentInfoProto.id` |
| Behaviour name | Trainer contract: `HeidelBotBehavior` (DeVita: `AnySpeedBotBehavior`), plus `?team=N` |
| Segment / joint index | Order of the body file. Joint order is the trainer-facing order: Spine, Spine2, Head, RUpLeg, RLeg, RFoot, RArm, RForeArm, RHand, LUpLeg, LLeg, LFoot, LArm, LForeArm, LHand |
| Body version | `heidelbot_notoes`, `heidelbot_notoes_h173`, … Each scaled or edited rig is its own body file |
| Layout version | `heidelbot_notoes_2` (Jolt-native, trained). `heidelbot_notoes_1` (Unity oracle, tests only). Any change to observation or action content bumps it |

## Code style

- Match master: `PascalCase` types, `snake_case` members, systems end in `Sys`, plain-struct
  components, EnTT 3.16 ctx API (`reg.ctx().emplace/get/find`).
- `OPTICK_EVENT()` at the top of every `tick`.
- Definitions loaded from disk (`BodyDef`, `PoseTrack`, `AgentParams`) are immutable after load and
  shared by `std::shared_ptr<const T>`. Mutable per-world state lives in that world's registry.
- No `rand()` in sim code; use the world RNG.
