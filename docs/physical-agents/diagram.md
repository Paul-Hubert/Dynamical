# Implementation-plan diagram — spec and Claude Design prompt

One diagram that explains the HumanBot port at a glance:
- what gets built;
- where each piece runs (which executable, which thread, which world);
- how data flows from Unity exports through training to the viewer;
- in which milestone each piece lands.

The source of truth is [`../physical-agents-plan.md`](../physical-agents-plan.md) and the docs in this
folder. When they change, update this spec, then regenerate the diagram.

## Audience and use

- **Audience:** Paul and collaborators who know ML-Agents and Unity but not this engine.
- **Read in about 60 seconds** at the top of the plan, in reviews and in progress reports.
- **Output:** a single landscape canvas at 16:9 or wider, exported as SVG and PNG into
  `docs/images/physical-agents-plan.svg` / `.png`.

## Content

### Lanes (left to right = data flow)

| Lane | Contents |
|---|---|
| **1. Unity (authoring only)** | `Dynamical-noToes` project: HeidelBot noToes prefab (16 bodies, 15 joints, Plagenhoef masses); Mecanim walk clip(s); Editor exporter (batch). Outputs, as file icons: `body.json`, `params.json`, `track.json/.bin`, `golden/*.json` |
| **2. Shared library `dynamical_sim`** | Scene loader (JSON: variables, systems, worlds, prefabs), system and component registries; World = `entt::registry` + `SystemSet` + Jolt `PhysicsSystem`; `RagdollBuilder`; agent runtime (Jolt-native layout heidelbot_notoes_2, 138×2 / 120, rewards, termination, episodes, energy recorder); `JoltRuntime` |
| **3. Executables** | `dynamical_train` = **TrainingEnv.exe** (headless; `TrainHost`; modes train / null / bench / eval / record-demo; links `dynamical_mlagents` gRPC and `dynamical_onnx`). `dynamical` (Vulkan, SDL, OpenXR; `WorldView`, panels; links `dynamical_onnx`) |
| **4. Scenes** | `walker_train` (in TrainingEnv.exe: 25 flat-ground worlds); `rig_anim` (in dynamical: kinematic pose-track playback plus live checks); `walker_view` (in dynamical: latest ONNX policy); `base` (in dynamical: marching-cubes world, unchanged) |
| **5. Python trainer (unchanged)** | `mlagents-learn` fork, configs, GAIL, W&B, `start-run` / `stop-run`. Outputs: `results/<run>/*.onnx`, TensorBoard / W&B stats |

### Edges (label each)

1. `body.json` and `params.json` → scene loader ("loaded at world init, hash in run metadata").
2. `track.bin` → `rig_anim` ("kinematic playback, validation").
3. `track.bin` → TrainingEnv.exe `record-demo` → `.demo` → trainer GAIL ("cached by track, body,
   layout and params hash").
4. Trainer ⇄ TrainingEnv.exe: a bidirectional thick edge, "gRPC, ML-Agents protocol 1.5.0: obs,
   rewards and stats out; actions, env params and RESET in".
5. Trainer → `results/*.onnx` → `walker_view` ("hot-reload of the newest checkpoint; separate process").
6. `golden/*.json` → `dynamical_tests` ("Unity oracle and regression").
7. TrainingEnv.exe `eval` → CSV and `EVT|` lines → existing analysis scripts.
8. Replays → `walker_view` and the Unity `ReplayPlayer` ("videos").

### Inset: one training window inside TrainingEnv.exe

A small sequence or loop strip:

```
exchange (gRPC) → scatter actions → Taskflow: N worlds in parallel × decision_period ticks
   [Actions@act → DriveTargets → Disturbances → Jolt step → BodySync → Energy → Contacts → Stats → Termination → Observation@observe → Episode@observe]
→ gather Outboxes (world order) → exchange
```

Callout: "kill mid-window: terminal snapshot, world parks until the window ends (lockstep kept)".

### Milestone overlay

Colour or badge each box with the milestone that delivers it:

| Badge | Boxes |
|---|---|
| P0 | Test and capture harness (tests target, capture, `imgdiff`, CI) |
| S1 | Scene loader, registries, CMake split, `base` scene |
| S2 | Worlds, `JoltRuntime`, Jolt 5.6.0 overlay, flat ground, `WorldView`, `bench` |
| M0 | gRPC bridge, `null` mode |
| M1 | Exporter, body loader, `RagdollBuilder`, **`rig_anim`** |
| M2 | Joints, `PositionAndVelocity` motors, patches P-A/P-C, contacts, energy recorder |
| M3 | Layouts, rewards, termination, episodes |
| M5 | Demo recorder (before M4) |
| M4 | Full training through TrainingEnv.exe, `start-run -Engine dynamical` |
| M5b | **`walker_view`** with ONNX, minimal eval |
| M6 | First GAIL walker (gate) |
| M7–M9 | Slope, DeVita stats, eval, replays, extended lines (greyed, "later") |

Add a thin timeline strip along the bottom: P0 → S1 → S2 → M0 → M1 → M2 → M3 → M5 → M4 → M5b → M6 → M7 → M8 → M9.
Show each gate as a diamond with a two-to-four-word gate label:

| Milestone | Gate label |
|---|---|
| P0 | "captures deterministic" |
| S1 | "base unchanged" |
| S2 | "25 worlds deterministic" |
| M0 | "trainer ceiling measured" |
| M1 | "rest pose 1e-5 m" |
| M2 | "limits ±1°, energy checks" |
| M3 | "oracle features match" |
| M4 | "unmodified config trains" |
| M5 | "GAIL loads demos" |
| M6 | "matches Unity reference" |

### Explicitly out of the picture

- Marching-cubes terrain in training: training is flat ground only.
- Heightfields.
- VR.
- The Unity games.

Show them only as a greyed "later" note.

## Visual guidance

- Five vertical lanes with light background bands and lane titles on top. Unity and Python lanes are
  muted, to read as "external, unchanged or authoring only"; the engine lanes are prominent.
- Rounded rectangles for components, file glyphs for data files, a cylinder for `results/`, an
  executable chip for each `.exe`. The bidirectional gRPC edge is the thickest line on the canvas.
- One accent colour per executable. `dynamical_train` and `dynamical` must never share a colour.
  Milestone badges are small pills in the top-right corner of boxes.
- Font sizes: at least 12 px equivalent for labels, 16 to 18 for lane titles. No paragraph text
  inside boxes, at most 2 lines each.
- It must read in both light and dark themes and survive greyscale printing: never encode meaning
  in colour alone; badges carry text.

## Claude Design prompt

Paste this into Claude Design (claude.ai). Attach this file and `../physical-agents-plan.md` as
context if the tool accepts attachments.

```text
Design a single landscape technical architecture diagram (16:9, export SVG + PNG) titled
"HumanBot on Jolt — implementation plan". It explains how a Unity-trained ML-Agents walker is
rebuilt as a native C++/Jolt simulation inside the "Dynamical" engine, while the Python trainer
stays unchanged.

Layout: five vertical lanes, left to right, following the data flow:
1. "Unity — authoring only" (muted): HeidelBot noToes prefab (16 bodies, 15 joints, Plagenhoef
   masses), Mecanim walk clip, Editor exporter. Output file glyphs: body.json, params.json,
   track.bin, golden/*.json; a small 'Unity importer' box on the engine side converts them.
2. "dynamical_sim (shared C++ library)": Scene loader (JSON: variables, systems, worlds, prefabs);
   System & component registries; World = EnTT registry + SystemSet + Jolt PhysicsSystem;
   RagdollBuilder; Agent runtime (Jolt-native layout, obs 138×2, actions 120, rewards, termination, episodes, exact
   energy recorder); JoltRuntime.
3. "Executables": chip "TrainingEnv.exe (dynamical_train, headless)" containing TrainHost with
   modes train / null / bench / eval / record-demo, linking gRPC bridge and ONNX Runtime; chip
   "dynamical (Vulkan + SDL + OpenXR)" containing WorldView and ImGui panels, linking ONNX Runtime.
   Give the two executables different accent colours.
4. "Scenes": walker_train (25 flat-ground worlds, inside TrainingEnv.exe); rig_anim (kinematic
   pose-track playback + live rig checks, in dynamical); walker_view (latest ONNX policy, in
   dynamical); base (marching-cubes world, unchanged, in dynamical).
5. "Python trainer — unchanged" (muted): mlagents-learn fork, configs, GAIL, W&B, start-run /
   stop-run; a cylinder "results/<run>/*.onnx".

Labelled edges:
- body.json + params.json → Scene loader: "loaded at world init; hashed into run metadata"
- track.bin → rig_anim: "kinematic playback (validation)"
- track.bin → TrainingEnv.exe record-demo → ".demo" → GAIL: "cached by track/body/layout hash"
- TrainingEnv.exe ⇄ mlagents-learn, the thickest line on the canvas: "gRPC · ML-Agents 1.5.0 —
  obs/rewards/stats out, actions/env params/RESET in"
- results/*.onnx → walker_view: "hot-reload newest checkpoint · separate process"
- golden/*.json → "dynamical_tests": "Unity oracle & regression"
- TrainingEnv.exe eval → "CSV + EVT lines" → "existing analysis scripts"

Inset (bottom-right), titled "One decision window in TrainingEnv.exe": exchange → scatter actions →
"N worlds in parallel (Taskflow) × decision_period ticks": Actions → DriveTargets → Disturbances →
Jolt step → BodySync → Energy → Contacts → Stats → Termination → Observation → Episode → gather →
exchange. Small callout: "kill mid-window → terminal snapshot, world parks until window end".

Milestones: put a small text pill on each box with the milestone that delivers it:
- P0: test and capture harness
- S1: scene loader, registries, build split, base scene
- S2: worlds, JoltRuntime, Jolt 5.6 overlay, flat ground, WorldView
- M0: gRPC bridge, null mode
- M1: exporter, body loader, RagdollBuilder, rig_anim
- M2: joints, motors, contacts, energy recorder
- M3: layouts, rewards, termination, episodes
- M5: demo recorder (before M4)
- M4: full training via start-run -Engine dynamical
- M5b: walker_view with ONNX
- M7–M9: greyed

Along the bottom, a timeline strip P0 → S1 → S2 → M0 → M1 → M2 → M3 → M5 → M4 → M5b → M6 → M7 → M8 → M9.
Each gate is a diamond with a short label:
P0 "captures deterministic", S1 "base unchanged", S2 "25 worlds deterministic", M0 "trainer ceiling measured",
M1 "rest pose 1e-5 m", M2 "limits ±1°, energy checks", M3 "oracle features match",
M4 "unmodified config trains", M5 "GAIL loads demos", M6 "matches Unity reference".

A small greyed "later" note: heightfield terrain, marching-cubes terrain for agents, VR,
strike/duel lines.

Style: clean engineering diagram, rounded rectangles, file glyphs for data, a cylinder for results,
generous whitespace, labels at least 12 px, lane titles 16–18 px, at most two lines of text per box.
Muted lanes for external systems (Unity, Python). Works in light and dark themes and in greyscale:
never rely on colour alone, every badge has text. No decorative illustrations, no gradients, no 3D.
```

## Checklist before accepting a render

- [ ] Every edge in "Edges" is present and labelled; the gRPC edge is the most prominent.
- [ ] The two executables have distinct colours. Every scene sits in the correct executable.
- [ ] Training is shown on flat ground only, and the marching-cubes `base` scene is visibly separate
      from training.
- [ ] The noToes numbers are correct: 16 / 15, obs 138 × 2, 120 actions.
- [ ] Milestone pills match the plan's milestone table.
- [ ] It reads in greyscale.
