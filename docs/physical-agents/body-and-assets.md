# Body, scenario and motion assets

Unity stays the source of the body and the motion data (Unity plan D1). A Unity editor exporter writes
three kinds of file into `resources/agents/`; the engine only reads them. All numbers are in the sim
frame ([conventions.md](conventions.md)): Unity's own coordinates, metres, kilograms, seconds.

| File | Format | Loaded into | Written by |
|---|---|---|---|
| `bodies/<name>.body.json` | JSON (cereal) | `BodyDef` | Unity exporter |
| `scenarios/<name>.scenario.json` | JSON (cereal) | `ScenarioDef` | Hand-written, defaults from the Unity plan |
| `tracks/<clip>.pose.bin` + `.pose.json` header | binary + JSON | `PoseTrack` | Unity exporter (baked from the Unity animation clips) |
| `models/<behaviour>-<run>.onnx` | ONNX | `PolicyRunner` | ML-Agents export (`stop-run`) |

Every file has a `"format_version"` integer; loaders refuse unknown versions.

## Body file — `BodyDef`

```jsonc
{
  "format_version": 1,
  "name": "humanbot_v3",
  "source": { "unity_prefab": "Assets/HumanBot/HumanBot.prefab", "exporter_version": "1.0.0", "export_date": "2026-11-02" },
  "solver": { "position_iterations": 6, "velocity_iterations": 1, "fixed_dt": 0.008333 },
  "materials": [ { "name": "foot", "dynamic_friction": 0.8, "static_friction": 0.8, "bounciness": 0.0,
                   "friction_combine": "Average", "bounce_combine": "Average" } ],
  "segments": [
    {
      "name": "hips", "parent": -1,
      "rest_pos": [0.0, 0.95, 0.0], "rest_rot": [0, 0, 0, 1],            // world, in rest pose (x,y,z,w)
      "mass": 11.2, "center_of_mass": [0, 0.02, 0],                       // local
      "inertia_tensor": [0.09, 0.11, 0.08], "inertia_tensor_rotation": [0, 0, 0, 1],
      "drag": 0.0, "angular_drag": 0.05, "max_angular_velocity": 50.0,
      "continuous_collision": false,
      "colliders": [
        { "type": "capsule", "center": [0, 0, 0], "radius": 0.12, "height": 0.34, "direction": 0,  // Unity: 0=X 1=Y 2=Z
          "rotation": [0, 0, 0, 1], "material": "default" }
      ]
    }
  ],
  "joints": [
    {
      "name": "hip_l", "parent": 0, "child": 1,
      "anchor": [0, 0, 0], "connected_anchor": [-0.09, -0.07, 0],        // child-local / parent-local
      "axis": [1, 0, 0], "secondary_axis": [0, 1, 0],
      "low_twist_deg": -30, "high_twist_deg": 90, "swing_y_deg": 40, "swing_z_deg": 30,
      "enable_collision": false,
      "drive": { "mode": "slerp", "spring": 4000, "damper": 100, "max_force": 600 },
      "action_dofs": ["twist", "swing_y", "swing_z"],                     // which axes the policy drives
      "limit_probes": [ { "axis": "twist", "at": "low",  "child_local_rot": [ /* quat */ ] } ]
    }
  ],
  "ignore_pairs": [ [3, 7] ],                                               // extra self-collision exclusions
  "rest_pose_track": "tracks/rest.pose.bin"
}
```

Required properties of the exporter:

- Export what Unity **actually used**, read back from the live components after `Awake`
  (`Rigidbody.centerOfMass`, `inertiaTensor`, `inertiaTensorRotation`), not what the inspector shows.
- Export every value in the [parity checklist](physics.md#parity-checklist-body-file-must-make-each-explicit),
  even when it is Unity's default.
- Joint `anchor`, `axis`, `secondary_axis` and limits exactly as the `ConfigurableJoint` stores them;
  the conversion lives in C++ (`RagdollBuilder`), not in the exporter.
- `limit_probes`: for each limited axis, drive the child to the limit in Unity and record the child's
  local rotation. These feed the limit unit tests.
- Segment order = the order the observation layout uses (Unity plan D3). Never re-sort.

Loading: `BodyDef loadBody(path)` validates (parents before children, every joint references existing
segments, masses > 0, unit quaternions within 1e-4) and converts `*_deg` to radians.

## Scenario file — `ScenarioDef`

One file per training line (walk, commanded walk, slope, strike, duel). It holds everything that is not
the body:

```jsonc
{
  "format_version": 1,
  "name": "walker_gail",
  "body": "bodies/humanbot_v3.body.json",
  "behaviors": [ { "name": "Walker", "team": 0, "agents_per_arena": 1 } ],
  "physics_hz": 120, "collision_steps": 1, "decision_period": 4,
  "max_episode_steps": 1000, "max_steps_unit": "decisions",
  "drive_mode": "jolt_motor",
  "ground": { "type": "box", "size": [200, 1, 200], "slope_deg": 0, "material": "ground" },
  "arena_spacing": 0,                        // arenas are separate worlds; spacing only matters for drawing
  "spawn": { "pose": "rest", "height_offset": 0.02 },
  "observations": "humanbot_v3",             // name of a registered ObservationLayout
  "actions": "humanbot_v3",                  // name of a registered ActionLayout
  "reward": { "terms": [ /* weights from the Unity plan */ ] },
  "termination": { /* fall height, illegal contacts, from the Unity plan */ },
  "randomisation": { /* defaults; overridable by env params */ },
  "env_param_defaults": { "target_speed": 1.3 }
}
```

- `ScenarioDef` values are the **defaults**. Values received on the Environment Parameters side channel
  override them by key at the next episode start ([agent-runtime.md](agent-runtime.md#environment-parameters)).
- Observation, action and reward code is C++ (registered by name), not data: the file selects which
  layout and supplies weights and thresholds.

## Pose tracks — `PoseTrack`

Motion clips baked from Unity, used for demos (M5), the rest pose, reset-to-reference-state, replay and
parity tests.

`<clip>.pose.json` (header):

```jsonc
{ "format_version": 1, "body": "humanbot_v3", "clip": "walk_1_3ms", "frame_rate": 120,
  "frames": 2400, "segments": 17, "has_velocities": true, "loop": true, "root_motion": true,
  "tags": { "speed": 1.3 } }
```

`<clip>.pose.bin`: little-endian `float32`, frame-major. Per frame, per segment in body order:
`pos.xyz, rot.xyzw` (+ `lin_vel.xyz, ang_vel.xyz` if `has_velocities`) — 7 or 13 floats.

- Sampled at a rate that is a multiple of the physics rate, so playback needs no interpolation in
  parity tests. If not, `PoseTrack::sample(t)` interpolates (lerp / slerp) and derives velocities by
  finite differences.
- Poses are full segment world transforms, not joint angles: the engine needs no animation retargeting.
  Joint angles are derived with the same code `BodySyncSys` uses.

## Later: glTF instead of the Unity bake

Once the engine has a mesh path, a glTF importer (cgltf via vcpkg) can read the skinned mesh and
animation clips directly. The body file still comes from Unity (physics properties do not live in
glTF), with a `mesh` field mapping segments to glTF nodes. Not needed before M8.
