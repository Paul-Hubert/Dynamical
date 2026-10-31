# `rig_anim` — kinematic rig validation scene

**Purpose.** See the rig before trusting it. `rig_anim` builds the ragdoll from the body file with
**kinematic** bodies, plays Unity pose tracks onto them, and checks live everything later milestones
depend on. No drives, policy or trainer are involved.

It is the visual front end of the M1 gate and the debugging tool for M3 parity and M5 demos. The
demo recorder runs the same playback path headless.

## Scene

```jsonc
// resources/scenes/rig_anim.scene.json
{
  "format_version": 1,
  "name": "rig_anim",
  "include": ["common/desktop.json"],
  "variables": {
    "body":   { "type": "string", "default": "agents/bodies/heidelbot_notoes.body.json" },
    "track":  { "type": "string", "default": "agents/tracks/walk.track.json" },
    "params": { "type": "string", "default": "agents/params/walker.params.json" },
    "speed":  { "type": "float",  "default": 1.0 }
  },
  "renderer": { "passes": ["model", "ui"], "vr": false },
  "host": {
    "pre":  ["Input", "UI", "PlayerControl", "DebugMenu", "RigPanel",
             { "name": "WorldStepper", "params": { "realtime": true, "speed": "${speed}" } }],
    "post": [{ "name": "WorldView", "params": { "worlds": "all", "show_frames": true } }]
  },
  "worlds": [{
    "physics": { "threads": 1, "max_bodies": 64 },
    "clock":   { "dt": "${params}#dt", "decision_period": "${params}#decision_period" },
    "fixed":   ["TrackPlayer", "PhysicsStep", "BodySync", "Contacts", "RigChecks", "Observation@observe"],
    "entities": [
      { "prefab": "ground_flat" },
      { "prefab": "humanbot", "AgentSpawn": { "body": "${body}", "params": "${params}", "motion": "kinematic" },
        "PoseTrackPlayer": { "track": "${track}", "loop": true } }
    ]
  }]
}
```

## Systems

| System | Where | Does |
|---|---|---|
| `TrackPlayer` | world, every tick | Reads frame `f` of the `PoseTrack` and calls `MoveKinematic` on each segment body (so Jolt computes the implied velocities and contacts). Supports play, pause, step ±1 frame, scrub, loop, and an alternative "teleport" mode (`SetPositionAndRotation`) |
| `BodySync` | world | The same system as in training: finite-difference velocities and joint angles. **No separate code path for validation** |
| `RigChecks` | world | Per tick: the checks below; violations go to a ring buffer with tick and frame |
| `Observation` | world, on observe ticks | The **real** observation layout on the kinematic pose, as the demo recorder does (UP §3.10) |
| `RigPanel` | host (ImGui/ImPlot) | Transport controls, track and body pickers, check results, plots |
| `WorldView` | host | Draws segment shapes (capsule/box glTF), optional bone axes, joint twist and swing arcs with limit wedges, foot contact markers |

## Checks

| Check | Pass | Feeds |
|---|---|---|
| Rest pose | Frame "rest" (from the body file) gives bone world poses equal to the body file to 1e-5 m, and joint angles read 0 | M1 gate |
| Track ↔ body | Track body name and `body_sha256` match the loaded body; segment count matches; `dt` equals world dt | loader |
| Joint limits | Every frame: each joint's twist and swing (computed by `BodySync`) are inside the limits. The panel reports the margin and the worst frame per joint | M1, M2 (cross-check of the twist sign and the elliptical cone against the limit probes) |
| Bone lengths | Parent-to-child anchor distance is constant over the track to 1e-4 m | Bake quality |
| Velocities | Finite-difference linear and angular velocities are free of spikes at loop seams | M5 (demo angular velocity) |
| Ground | Lowest foot point per frame against y = 0: penetration and float, contact on/off per foot | Stance stats, M5 |
| Observation | Observation vector printed with element names from the layout; optional diff against a golden file of the same frame | M3 |
| Root motion | Root path, heading and speed plotted against the track's tag speed | Demo and target-speed sanity |

`RigChecks` results can be exported to `rig_report.json` (`var.report=path`, and `var.headless=true` in
tests, where the scene runs in `dynamical_tests` without the renderer). This makes the check a CI test
as well as a viewer.

## Also used for

- **Limit probes.** Play the limit-sweep rig's export as a track. The child should sit exactly on the
  Jolt limit wedge.
- **Bake regressions.** Diff two tracks of the same clip, from different exporter versions, in one
  scene with two worlds side by side.
- **Mirroring check.** Visual confirmation that `simToEngine` produces a correct, non-mirrored-looking
  walker: the left leg is on the left.
