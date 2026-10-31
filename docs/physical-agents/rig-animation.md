# `rig_anim` — kinematic rig validation scene

**Purpose.** See the rig before trusting it. `rig_anim` builds the ragdoll from the body file with
**kinematic** bodies, plays Unity pose tracks onto them, and checks live everything later milestones
depend on. No drives, policy or trainer are involved.

It is the visual front end of the M1 gate and the debugging tool for M3 (features against the Unity oracle) and M5 demos. The
demo recorder runs the same playback path headless.

## Scene

The world lives in a shared fragment, so the same world runs rendered (`rig_anim`) and headless
(`rig_check` through `ScenarioRunner`, and the demo recorder's `walker_demo`).

```jsonc
// resources/scenes/common/rig_world.json
{
  "variables": {
    "body":   { "type": "string", "default": "agents/bodies/heidelbot_notoes.body.json" },
    "track":  { "type": "string", "default": "agents/tracks/heidelbot_notoes__BasicMotions@Walk.track.json" },
    "params": { "type": "string", "default": "agents/params/walker.params.json" },
    "report": { "type": "string", "default": "" }
  },
  "worlds": [{
    "physics": { "threads": 1, "max_bodies": 64 },
    "clock":   { "physics_hz": "${params}#physics_hz", "control_hz": "${params}#control_hz" },
    "fixed":   ["TrackActions", "PhysicsStep", "BodySync", { "name": "RigChecks", "params": { "report": "${report}" } }],
    "entities": [
      { "prefab": "ground_flat" },
      { "prefab": "humanbot", "AgentSpawn": { "body": "${body}", "params": "${params}", "motion": "kinematic" },
        "PoseTrackPlayer": { "track": "${track}", "loop": true } }
    ]
  }]
}
```

```jsonc
// resources/scenes/rig_anim.scene.json
{ "format_version": 1, "name": "rig_anim",
  "include": ["common/desktop.json", "common/rig_world.json"],
  "variables": { "speed": { "type": "float", "default": 1.0 } },
  "renderer": { "passes": ["model", "ui"], "vr": false },
  "host": { "pre":  ["RigPanel", { "name": "WorldStepper", "params": { "realtime": true, "speed": "${speed}" } }],
            "post": [{ "name": "WorldView", "params": { "worlds": "all", "show_frames": true } }] } }
// resources/scenes/rig_check.scene.json: { "include": ["common/rig_world.json"] }, run by ScenarioRunner
```

- **The fixed list grows by phase:**
  - P4: the rest pose only (no track);
  - P5: `TrackActions`, `RigChecks`;
  - P7: `Contacts` appended;
  - P8: `Observation@observe` appended, with the observation panel.
- **Kinematic bodies get no contacts from Jolt** against static bodies by default. The ragdoll builder
  sets `BodyCreationSettings::mCollideKinematicVsNonDynamic` for `motion = kinematic`, so contact
  markers work. The **ground checks in `RigChecks` are geometric** (lowest shape point against y = 0),
  so they do not depend on that flag.

## Systems

| System | Where | Does |
|---|---|---|
| `TrackActions` | world (lib), every tick | Reads frame `f` of the `PoseTrack` and calls `MoveKinematic` on each segment body. Play, pause, step ±1 frame, scrub, loop, and a "teleport" mode (`SetPositionAndRotation`) |
| `BodySync` | world (lib) | The same system as in training: finite-difference anchor velocities and joint angles. **No separate code path for validation** |
| `RigChecks` | world (lib) | Per tick: the checks below; violations go to a ring buffer with tick and frame; `rig_report.json` when `report` is set |
| `Observation` | world (lib), on observe ticks, from P8 | The **real** observation layout on the kinematic pose, as the demo recorder does |
| `RigPanel` | host (`dynamical`) | Transport controls, track and body pickers, check results, plots |
| `WorldView` | host (`dynamical`) | Segment shapes, bone axes, joint arcs with limit wedges, foot contact markers ([scenes.md](scenes.md#worldview-drawing-worlds)) |

## Checks

| Check | Pass | Feeds |
|---|---|---|
| Rest pose | The rest pose **before the knee move** equals the body file's Unity rest pose (imported) to 1e-5 m. After the move, joint angles read 0 except the knees (2.1°) | M1 gate |
| Track ↔ body | Track body name and `body_sha256` match the loaded body; segment count matches; `dt` equals world dt to a relative 1e-5 | loader |
| Joint limits | Every frame: each joint's twist and swing (computed by `BodySync`) are inside the limits. The panel reports the margin and the worst frame per joint | M1, M2 (cross-check of the twist sign and the elliptical cone against the limit probes) |
| Bone lengths | Parent-to-child anchor distance is constant over the track to 1e-4 m | Bake quality |
| Velocities | Finite-difference linear and angular velocities are free of spikes at loop seams | M5 (demo angular velocity) |
| Ground | Lowest foot point per frame against y = 0: penetration and float, contact on/off per foot | Stance stats, M5 |
| Observation | Observation vector printed with element names from the layout; optional diff against a golden file of the same frame | M3 |
| Root motion | Root path, heading and speed plotted against the track's tag speed | Demo and target-speed sanity |

`RigChecks` results can be exported to `rig_report.json` (`var.report=path`, and the headless `rig_check` scene in
`dynamical_tests`). This makes the check a CI test
as well as a viewer.

## Also used for

- **Limit probes.** Play the limit-sweep rig's export as a track. The child should sit exactly on the
  Jolt limit wedge.
- **Bake regressions.** Diff two tracks of the same clip, from different exporter versions, in one
  scene with two worlds side by side.
- **Import check.** Visual confirmation that the Unity importer's frame conversion produces a correct
  walker: the left leg is on the left, and it walks toward −z.
