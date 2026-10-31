# Assets

**Unity stays the authoring tool** (UP §3.1, §3.10).
- One Editor script, callable in batch mode, writes versioned files into `resources/agents/`.
- Each file is stamped with the source prefab or scene GUID, the git sha and the exporter version.
- All numbers are in the sim frame, i.e. Unity's own ([conventions.md](conventions.md)).

| File | Format | Loaded into | Written by |
|---|---|---|---|
| `bodies/<body>.body.json` | JSON | `BodyDef` | Exporter (UP §3.1) |
| `tracks/<clip>.track.json` + `.track.bin` | JSON header + float32 | `PoseTrack` | Pose-track bake (UP §3.10) |
| `params/<scenario>.params.json` | JSON | `AgentParams` | Exporter: the **resolved** scene values of `HumanBotParameters`, the energy provider and scene overrides (UP §3.1) |
| `golden/<body>/<case>.json` | JSON | tests | Unity rollout logger (UP §4 M3) |

Every file carries `format_version`; loaders refuse unknown versions.

## Body file

**The body file is part of the policy contract** (UP §3.1). Its SHA-256 goes into the run metadata
(`envparams.txt`, `scene.resolved.json`) and into the demo cache key.

```jsonc
{
  "format_version": 1,
  "name": "heidelbot_notoes",
  "source": { "prefab_guid": "…", "git_sha": "<noToes sha>", "exporter": "1.0.0" },
  "segments": [
    {
      "name": "Spine", "parent": -1,
      "rest_world_pos": [0, 0, 0], "rest_world_rot": [0, 0, 0, 1],     // bone origin (root origin is at the feet)
      "rest_local_pos": [0, 0, 0], "rest_local_rot": [0, 0, 0, 1],
      "scale": [1, 1, 1],                                               // export the real scale (gladiator feet are 1.0517, UP §3.1)
      "mass": 9.94, "center_of_mass": [0, 0, 0],
      "inertia_tensor": [0, 0, 0], "inertia_tensor_rotation": [0, 0, 0, 1],   // play-mode readback after Configure
      "linear_damping": 0, "angular_damping": 0.05, "max_angular_velocity": 20,
      "shapes": [
        { "type": "capsule", "center": [0, 0, 0], "rotation": [0, 0, 0, 1], "radius": 0.1, "height": 0.3, "direction": 1, "material": "heidelbot" },
        { "type": "box", "center": [0, 0, 0], "rotation": [0, 0, 0, 1], "half_extents": [0.1, 0.05, 0.12], "material": "heidelbot" }
      ]
    }
  ],
  "joints": [
    {
      "name": "RUpLeg", "parent": 0, "child": 3, "side": "R",                 // C / R / L
      "anchor": [0, 0, 0], "connected_anchor": [0, 0, 0],                       // runtime values
      "axis": [1, 0, 0], "secondary_axis": [0, 1, 0],
      "twist_low_deg": -20, "twist_high_deg": 70, "swing_y_deg": 30, "swing_z_deg": 30,
      "spring_factor": 1, "damper_factor": 1, "max_force": 200000,
      "limit_probes": [ { "dof": "twist", "at": "low", "child_local_rot": [0, 0, 0, 1] } ]
    }
  ],
  "materials": { "heidelbot": { "friction": 1, "friction_combine": "max", "bounciness": 0, "bounce_combine": "min" } }
}
```

The example values are placeholders; UP §3.1 and the exporter supply the real ones.

**Exporter requirements:**
- Read values back **in play mode, after `Configure`**: mass properties, connected anchors.
- Exclude eye triggers.
- Merge compound children (Spine1, Neck, Shoulders) into their parent segments, as Unity's
  compound colliders do.
- Joint order = UP §3.4's order; segment order = hierarchy order. Never re-sorted.
- `limit_probes`: the limit-sweep rig (UP §4 M2) records the child's local rotation at each limit.

**`loadBody` validates:**
- parents come before children, and every index is valid;
- masses are > 0;
- quaternions are unit to 1e-4;
- joint anchors coincide in world space at rest to 1e-5 m.

It converts degrees to radians.

## Pose tracks

Baked once per clip by playing the Mecanim controller on the animated clone at the physics rate
(UP §3.10).

```jsonc
// tracks/walk.track.json
{ "format_version": 1, "body": "heidelbot_notoes", "body_sha256": "…", "clip": "BasicMotions@Walk",
  "clip_guid": "…", "dt": 0.0166666, "frames": 3000, "segments": 16, "loop": true, "has_velocities": false }
```

- `.track.bin`: little-endian float32, frame-major. Per frame and per segment, in body order:
  `pos.xyz, rot.xyzw` (7 floats), plus `lin_vel.xyz, ang_vel.xyz` when `has_velocities`.
- The poses are **world poses of the body bones**, the ones Unity copies by name onto the kinematic
  bodies. No retargeting happens in C++.
- When needed, velocities are derived in C++: linear velocity by finite difference of the bone origin,
  angular velocity by quaternion finite difference (UP §3.10 open question; compare with a Unity demo
  in M5).
- `dt` must equal the world's physics dt (UP §3.1 gives the exact value). The loader refuses a mismatch instead of interpolating, so
  demos stay exact.
- The rest pose is not a track: it comes from the body file.

## Agent parameters

`AgentParams` is one struct (UP §3.3: `BotParameters` + `HumanBotParameters` + `EnergyRewardProvider`),
loaded from `params/*.params.json` and then overridden by environment parameters at initialisation and
at every episode start ([mlagents-bridge.md](mlagents-bridge.md#environment-parameters)).

- **Layout-changing flags are file-only**, never env-param overridable (UP §3.1): `springControl`,
  `angularVelocityControl`, `jointSpace`, `*Obs`, `freezeObs`, `normalizedVelocity`.
- They select the layout version; the loader rejects combinations the C++ layout does not implement.
- Physics settings (dt, physics rate, solver, gravity) and the arena description live in the same
  file, so one exported file pins a scenario.
- Unknown keys are an error in the file, and a **warning** when they arrive as env params (UP §3.9).

## glTF

For drawing:
- `resources/models/capsule.gltf` and `box.gltf` are scaled per shape by `WorldView`;
- a skinned HeidelBot glTF is optional, later (M8+).

Physics never reads glTF.
