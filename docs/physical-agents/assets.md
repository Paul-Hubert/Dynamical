# Assets

**Unity authors, the engine imports.**
- A Unity Editor exporter, callable in batch mode, writes **Unity's own values verbatim** into
  `resources/agents/`. Each file is stamped with the source prefab or scene GUID, the git sha and the
  exporter version.
- The C++ **Unity importer** (`src/agent/unity_import.*`) converts everything to the engine frame and
  SI units at load ([conventions.md](conventions.md)). All Unity-specific logic lives on the C++ side,
  in one place, so the exporter stays dumb.

| File | Format | Loaded into | Written by |
|---|---|---|---|
| `bodies/<body>.body.json` | JSON | `BodyDef` | Exporter |
| `tracks/<body>__<controller>.track.json` + `.track.bin` | JSON header + float32 | `PoseTrack` | Pose-track bake |
| `params/<scenario>.params.json` | JSON | `AgentParams` | Exporter, from a scene plus a config |
| `golden/<body>/<case>.json` | JSON | oracle tests | Unity rollout logger |

Every file carries `format_version`; loaders refuse unknown versions. The Unity reference for every
value is [unity-analysis.md](unity-analysis.md) ("UA").

## Body file

**The body file is part of the policy contract.** Its SHA-256 goes into the run metadata
(`envparams.txt`, `scene.resolved.json`), the demo cache key and the replay header.

**Body versions:**

| Version | Source | Notes |
|---|---|---|
| `heidelbot_notoes` | `HeidelBot.prefab`, scale 1 (1.89 m) | Flat walker, `DeVitaArena` |
| `heidelbot_notoes_h173` | Same prefab under the H173 root scale 0.9153439 (1.73 m) | DeVita H173 line. Shapes and anchors are scaled; masses come from `totalMass` |

```jsonc
{
  "format_version": 1,
  "name": "heidelbot_notoes",
  "source": { "prefab_guid": "b5543a35…", "git_sha": "815fb4b41", "exporter": "1.0.0", "root_scale": 1.0 },
  "frame": "unity_left_handed",                       // the importer converts
  "inertia_source": "physx_readback",                 // or "anthropometric"
  "angular_damping": 0.0, "linear_damping": 0.0,      // body-version choices (Unity: 0.05 / 0)
  "segments": [
    {
      "name": "HeidelBot", "parent": -1, "role": "pelvis",
      "rest_world_pos": [0, 0, 0], "rest_world_rot": [0, 0, 0, 1],      // bone origin; pelvis origin at floor level
      "body_percentage": 13.66,
      "center_of_mass": [0, 1.03, -0.03], "inertia_tensor": [0, 0, 0], "inertia_tensor_rotation": [0, 0, 0, 1],
      "shapes": [ { "type": "capsule", "source": "mixamorig:Hips", "center": [0, 1.0315, -0.031], "rotation": [0, 0, 0, 1],
                    "radius": 0.11363, "height": 0.35495, "direction": 0, "material": "heidelbot" } ]
    }
  ],
  "joints": [
    {
      "name": "RightLeg", "parent": "RightUpLeg", "child": "RightLeg", "side": "R",
      "axis": [1, 0, 0], "secondary_axis": [0, 1, 0],
      "low_x_deg": 2, "high_x_deg": 132.62, "y_deg": 15, "z_deg": 13,
      "spring_factor": 1, "damper_factor": 1, "torque_factor": 1,
      "limit_probes": [ { "dof": "twist", "at": "low", "child_local_rot": [0, 0, 0, 1] } ]
    }
  ],
  "joint_order": ["Spine", "Spine2", "Head", "RightUpLeg", "RightLeg", "RightFoot", "RightArm", "RightForeArm", "RightHand",
                  "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftArm", "LeftForeArm", "LeftHand"],
  "materials": { "heidelbot": { "friction": 1, "friction_combine": "max", "bounciness": 0, "bounce_combine": "min" } }
}
```

The values come from UA §1. The exporter reads the real ones; this doc never becomes their source.

**Exporter requirements:**
- Read back **in play mode after `Configure`**:
  - centre of mass and inertia (`physx_readback`; nothing in code sets them, so PhysX derives them from
    the colliders);
  - the auto-configured connected anchors.
- Export per-shape transforms, including the foot child objects and their 1.0517 scale. Drop triggers.
- Export compound children with their source names. The pelvis capsule sits on the unbodied Hips.
- Export the **joint order explicitly** (UA §1.4). Never derive it from hierarchy and side.
- `limit_probes` come from the limit-sweep rig. They are the authority for twist sign and cone shape
  after import.

**`loadBody` + importer validate and convert:**
- parents come before children, the indices resolve, `body_percentage` sums to 99.98 ± 0.05;
- quaternions are unit to 1e-4; anchors coincide in world space at rest to 1e-5 m;
- frame conversion and degrees → radians;
- the knee rest pose is moved to 2.1° ([agent-runtime.md](agent-runtime.md#actions-8--15)); the
  pre-move pose is kept for the Unity comparison tests;
- swing half-angles are clamped below 180° (with a log line).

## Pose tracks

Baked once per clip and body version by playing the Mecanim controller with root motion at the
controller's state speed, at the physics rate (UA §6, §9.2).

```jsonc
// tracks/heidelbot_notoes_h173__Walk1p500mps-h173.track.json
{ "format_version": 1, "body": "heidelbot_notoes_h173", "body_sha256": "…",
  "controller": "Walk1p500mps-h173", "clip": "BasicMotions@Walk01 - Forwards", "state_speed": 0.807316,
  "clip_guid": "…", "dt": 0.0166666, "frames": 20000, "segments": 16, "loop": false, "has_velocities": false,
  "frame": "unity_left_handed" }
```

- `.track.bin`: little-endian float32, frame-major. Per frame and per segment, in body order:
  `pos.xyz, rot.xyzw` (7 floats).
- **World poses of the 16 bodies**, matched by name, as Unity's recorder copies them. No retargeting
  happens in C++.
- `dt` must equal the world's physics dt to a relative 1e-5 (Unity's dt is 2351990/141120000 s,
  4e-6 off 1/60). The loader refuses a larger mismatch instead of interpolating.
- **Velocities are not stored.** C++ derives them: finite differences of the anchors for observations,
  and `MoveKinematic` for the bodies.
- **One track per controller.** Controllers that chain states (Ladder, Leans) are baked as one long
  track per episode sequence.

## Agent parameters

`AgentParams` is one struct. It is loaded from `params/*.params.json`, then overridden by env params at
initialisation and every episode start ([mlagents-bridge.md](mlagents-bridge.md#environment-parameters)).

- **Resolved values only.** The exporter writes the prefab plus scene-override values, never C# field
  defaults. They differ (UA §8):

  | Field | Resolved (prefab) | C# default |
  |---|---|---|
  | `pushForce` | 5 | 1 |
  | `decisionPeriod` | 2 | 5 |
  | `maxAngularVelocity` | 20 | 10 |
  | `totalMass` | 70 | 80 |
  | `normalizeSpeedDiff`, `relativeVelocity`, `normalizedVelocity`, `targetSpeedObs`, `dyingObs` | off | on |
- **One file per line:**

  | File | Content |
  |---|---|
  | `walker.params.json` | `Training.unity`: behaviour `HeidelBotBehavior`, body `heidelbot_notoes`, 70 kg, `energyBonusWeight 0`, `rootReward 0.34`, randomised target and speed |
  | `devita_h173.params.json` | `DeVitaArenaH173.unity`: behaviour `AnySpeedBotBehavior`, body `heidelbot_notoes_h173`, 69 kg, `rootReward 0` |

  Config env params (gains, energy, slope) arrive at runtime as env params, as in Unity.
- **Layout-defining flags are file-only.** `springControl`, `angularVelocityControl`, `jointSpace`,
  `*Obs`, `freezeObs`, `normalizedVelocity` and `relativeVelocity` select the layout. **This is the one
  list.** An env param equal to the file value is accepted. A different value is fatal (exit 1).
- **Durations** are Unity's 60 Hz counts: `physics_hz` (60), `control_hz` (30), `max_frames_60hz`
  (2000), `arc_frames_60hz` (400), `substeps` (default 1).
- **Physics** (rate, solver, gravity, `drive_gain_scale`) and the arena (ground box, world count) live
  in the same file, so one file pins a scenario.
- **Unknown keys** are an error in the file and a warning when they arrive as env params.

## Golden rollout files

`golden/<body>/rollout_<n>.json` comes from the Unity rollout logger (phases P8). It feeds the `[oracle]`
tests.

```jsonc
{ "format_version": 1, "body": "heidelbot_notoes", "body_sha256": "…", "params": { /* resolved values */ },
  "scene": "Training.unity", "git_sha": "…", "dt": 0.016666596, "decision_period": 2,
  "est0": 1.534,                                  // Unity's spawn-pose head–foot height (UA §3.3)
  "tick_phase": "post_fixedupdate",               // poses are logged after physics, before the next FixedUpdate
  "ticks": [ { "t": 0, "poses": [[px,py,pz,qx,qy,qz,qw], …16],
               "spine_rb_velocity": [vx,vy,vz],    // PhysX Rigidbody.linearVelocity (header input, UA §2.2)
               "last_position": [[x,y,z], …15],    // BotJoint.lastPosition per joint (finite-difference state)
               "target": [x,y,z], "target_speed": 1.36 } ],
  "decisions": [ { "t": 1, "obs": [ …276 ], "actions": [ …120 ], "reward_terms": { "root": 0.0, "death": 0.0 },
                   "done": false, "max_step_reached": false } ] }
```

All values are in Unity's frame. The tests convert them with the same importer as the body.

## Demo cache key

`.demo` files are cached under `Demonstrations/` by the hash of:
- the track;
- the body;
- the layout version;
- the params that change demo observations (`relativeVelocity`, `normalizeSpeedDiff`,
  `speedDiffZeroRecording`, recording episode length).

## glTF

For drawing:
- `resources/models/capsule.gltf` and `box.gltf` are scaled per shape by `WorldView`;
- a skinned HeidelBot glTF is optional, later (M8+).

Physics never reads glTF.
