# Sphere roller (pre-P4 side task)

A minimal agent that exercises the whole training path before the ragdoll (P4+): a scene of agent worlds,
`TrainingEnv.exe mode=train`, the unchanged ML-Agents trainer, PPO. A dynamical sphere on rolling hills learns to
travel at a target velocity that drifts during the episode, with a single torque vector as its action.

## Scenes

**`sphere_roll`** (training; `resources/scenes/sphere_roll.scene.json`, template `common/sphere_world.json`): the
marching cubes terrain of `base`, 25 worlds by default (`var.num_worlds`). Each world has

- `WorldTerrain`: `TerrainColliders` on the world's own PhysicsWorld, with base's generator and seed
  (`var.terrain_seed` = 1337). Cells of 16³ marching cubes (64 m, 4 m cubes) are built on the CPU, inline and in a
  fixed order, before each step around the sphere (its bounds + 2 m, and 6 m further down); cells unused for 5 s
  of simulation are evicted. The world is infinite: no bounds, homing off;
- spawns anywhere within `var.spawn_radius` = 2000 m of the origin: the column around the spawn point is built
  first from the generator's height range there, then a ray cast finds the top surface;
- an episode also ends (truncated) if the sphere falls 100 m below its spawn point.

**`sphere_roll_hills`** (`common/sphere_world_hills.json`): the first version, kept for tests: a `HeightField` of
256 × 256 samples, 0.5 m apart (127.5 m square), heights from 6 plane waves with wavelengths log-uniform in
[6, 40] m, each wave's steepest slope `wave_slope` = 0.07 (typical slopes 5 to 10°); the waves are drawn from the
world's RNG, so each world and each trainer seed has its own terrain; `var.wave_slope=0` gives flat ground.
Episodes end (truncated) outside ±55 m; beyond 30 m from the spawn point new goal headings point back to it.

Both: a sphere of radius 0.25 m, 10 kg (inertia from the shape), friction 1 (terrain cells 0.8, height field 1,
geometric mean), restitution 0; physics at 60 Hz, decisions at 15 Hz (a window of 4 ticks), sleeping off.
Fixed lane: `RigidBody`, (`WorldTerrain`), `SphereControl`, `PhysicsStep`, `SphereTask`, `SphereObserve@observe`.

## Goal frame

Every observation vector is expressed in the goal frame of the live target:

- y: world up;
- z: the target direction (horizontal);
- x: up × z.

In the engine's right-handed Y-up axes, x = up × forward points to the agent's **left**; Unity, left-handed, calls
the same vector right. Keeping x = up × z makes (x, y, z) a proper rotation of the world (a yaw), so cross products
carry over: torques and angular velocities mean the same thing in both frames (SR.1).

## Action

3 continuous values in [−1, 1] (clamped; NaN → 0): a torque in the goal frame of the observation the action
answers, × `max_torque` = 20 N·m, converted to a world torque once at the start of the window and applied to the
sphere on every tick of the window (Jolt clears forces after each step). It is a world-space torque, not a
body-local one: the sphere's own orientation plays no part. The policy sees nothing in world axes, so a torque in
world axes could not be learned for arbitrary target directions; the goal frame is what makes the action mean the
same thing whatever the target heading.

Rolling without slipping, v = r (ω × up): a torque along up × Δv accelerates the sphere along Δv. Full torque gives
about 5.7 m/s² on flat ground (I = 2/5 m r²); the friction it needs (μ ≈ 0.58) is below the contact's 1.

## Observations (16 floats)

| Index | Value |
|---|---|
| 0–2 | ground normal under the sphere |
| 3–5 | ground normal `probe_distance` = 1 m ahead along the horizontal velocity (along the target when slower than 0.05 m/s) |
| 6–8 | ground normal 1 m ahead along the target direction |
| 9–11 | sphere velocity, m/s |
| 12–14 | sphere angular velocity, rad/s |
| 15 | target speed, m/s |

Normals are ray casts straight down onto the STATIC layer (`PhysicsWorld::groundBelow`): the face normal of the
triangle hit (marching cubes triangles are about 4 m, height field triangles 0.5 m). Zero when there is no ground
within reach: off the height field, or on marching cubes a drop of more than the 6 m of cells built below. The trainer normalises the
observations (`normalize: true`).

## Target and episodes

- The target is a heading and a speed. At reset: a random heading, a speed in [0.5, 3] m/s (or 0 with
  probability 0.1), held for one goal interval.
- Then every 2 to 6 s a new goal is drawn: a heading within ±120° of the current one (pointing back to the origin
  ± 45° when homing is on and the sphere is more than `home_radius` from its spawn point), and a new speed. The target turns toward the goal at 0.7 rad/s and
  changes speed at 0.75 m/s²: it changes gradually, every tick.
- Episodes last 600 decisions (40 s). Leaving the bounds (height field) or falling 100 m ends the episode at the end
  of the window (the world is parked). Every end is a truncation (`max_step_reached`): the policy sees neither its
  position nor the clock.
- The trainer's RESET starts each agent at a random point of its first episode (`stagger`), so the worlds do not
  all end, and rebuild their ground, on the same step.
- A done agent gets no action in the next STEP; its world resets at the start of that window, steps it with zero
  torque, and sends the new episode's first observation (reward 0).

## Reward

Per tick, after the physics step: r = exp(−|v_xz − v*| / `reward_scale`), `reward_scale` = 1 m/s, where v_xz is the
sphere's horizontal velocity. The window's reward is the mean over its ticks, in [0, 1].

**v\* is the target velocity of the observation the policy acted on** (`SphereAgent::decision_target`, frozen by
the Observe phase), not the live target, which keeps drifting during the window. The next observation carries the
new target, which becomes the next window's reference. SR.4 checks it with the target swinging hard within the
window.

Stats sent on the stats side channel (averaged over agents per window, then by the trainer over the summary
period): `Sphere/Speed error` (mean |v_xz − v*|, m/s) and `Sphere/Out of bounds` (fraction of episode ends).

## Baselines (SR.6, SR.11)

8 worlds × 900 decisions, mean window reward, for a proportional controller (torque = clamp(up × (v* − v)),
gain 1) and for zero torque:

| Ground | Controller | Zero torque |
|---|---|---|
| marching cubes (`sphere_roll`) | 0.61 | 0.08 |
| height field (`sphere_roll_hills`) | 0.85 | 0.20 |

A trained policy should reach the controller or beat it (it can anticipate slopes from the normals).

## Training

`training/sphere_roller_ppo.yaml`: PPO, 256 × 2 network, normalisation, batch 2048, buffer 40960, 3 epochs,
γ 0.99, λ 0.95, linear learning-rate decay to max_steps.

```bash
build/venv/Scripts/python tools/train_sphere.py --run-id sphere_night --num-envs 16 --max-steps 200000000
build/venv/Scripts/tensorboard --logdir build/runs
build/venv/Scripts/python tools/train_sphere.py --stop sphere_night     # saves and exports ONNX
```

The launcher freezes a copy of `TrainingEnv.exe`, its DLLs and `resources/{scenes,prefabs}` in
`build/runs/<run-id>_env`, keeps wandb offline and keeps the machine awake while it runs. Checkpoints (with
ONNX) every 5M steps in `build/runs/<run-id>/SphereRoller/`.

## Inference and videos

Policy inference came ahead of P11 for this agent (`src/onnx`, library `dynamical_onnx`):

- **ONNX Runtime 1.23.2**, Microsoft's prebuilt CPU archive, downloaded by `cmake/onnxruntime.cmake` into
  `build/_deps` and pinned by SHA-256 (the vcpkg port builds from source). `onnxruntime.dll` is copied next to
  `dynamical.exe` and `dynamical_tests.exe` (System32 has an older one of the same name). `-DDYNAMICAL_ONNX=OFF`
  builds without it.
- **`PolicyRunner`** loads a checkpoint and validates it against the layout (input `obs_0` of 16, 3 continuous
  actions, `memory_size` 0). The fork's export holds the normaliser and the trainer's clamp(−3, 3)/3, so raw
  observations go in and `deterministic_continuous_actions` come out as the env actions (`continuous_actions` adds
  the sampled noise). SR.13: the engine's outputs equal Python onnxruntime's on 100 observations (difference 0).
- **`Policy`** (world, Act phase) runs the world's `PolicyHandle` on its AgentIO. Training templates hold the slot
  `TrainerActions@act` (a no-op: the training host writes the actions); viewer scenes replace it.
- **`ModelWatcher`** (host) loads `model` (a file, or `latest:<dir>` for the largest `<behavior>-<step>.onnx`,
  polled every 2 s and loaded once two polls agree on size and time) into every world. With `latest:` the viewer
  follows a training run as it writes checkpoints.

Scenes: **`sphere_view`** (`var.model=<file.onnx>` or `var.model=latest:build/runs/<run>/SphereRoller`) and
**`sphere_view_heuristic`** (the proportional controller), both on `sphere_world` with the WorldTerrain margin
raised to 8 m for camera collision. `SphereView` (host) follows the sphere from behind and 0.5 rad to the side,
draws the target velocity (green bar) and the horizontal velocity (orange bar, 0.6 m per m/s), three dots that turn
with the sphere, and a panel (checkpoint, target, speed, error, window reward, decision). `seed=<n>` picks the spawn
point and the targets.

```bash
build/RelWithDebInfo/dynamical.exe scene=sphere_view var.model=latest:build/runs/sphere_mc_night_20261031/SphereRoller seed=3
build/venv/Scripts/python tools/sphere_video.py --seconds 30 --seed 1            # newest checkpoint of the newest run
build/venv/Scripts/python tools/sphere_video.py --seconds 30 --seed 1 --heuristic
```

`tools/sphere_video.py` writes a capture script with a frame `sequence` (new in the capture harness:
`<name>_00000.png` every `every` frames, camera untouched), runs `dynamical.exe` on the fixed 60 Hz frame clock,
and encodes the frames with the ffmpeg of `imageio-ffmpeg` (build/venv) into
`build/artifacts/sphere_video/<run>_<step>M_seed<n>.mp4` (960 × 540, real time; 30 s render in about a minute).

SR.16: through `Policy`, the 55M-step checkpoint (`tests/data/onnx/sphere_roller.onnx`) gets 0.85 per decision
against the controller's 0.61 on the same 8 marching cubes worlds.

## Player (`vr_roller`)

The trained spheres are the players' locomotion: `dynamical.exe scene=vr_roller` (base's renderer with VR and grass).
Two players at once, each on their own sphere in their own world: the VR player on the small sphere, world 0 (orange),
and the desktop player on the wheel, world 1 (blue). `base` is unchanged.

- **Policies** (`ModelWatcher`, `sphere_world` with `Policy@act` in both worlds):
  - world 0: `var.model`, by default `resources/policies/sphere_roller.onnx`, the final checkpoint of
    `sphere_mc_night_20261031` (200M steps);
  - world 1: `var.desktop_model`, by default `latest:runs/*/WheelRoller|resources/policies/wheel_roller.onnx`.
    That is the newest `WheelRoller` checkpoint of `build/runs`, reloaded as training writes new ones, else the
    shipped wheel, the final checkpoint of `wheel_50m_20261101` (50M steps, mean episode reward 544).
  - `ModelWatcher` takes `models: { "<world>": <spec> }` over `model`. `latest:` accepts one `*` path component,
    which picks the run whose newest checkpoint is the most recent. `|<file>` is loaded while no checkpoint
    exists. `PolicySources` holds what each world runs, and `SphereView` shows its own world's.
- **Two worlds.** Two world entries of `sphere_world`. The second takes `variable_files: ["common/wheel_body.json"]`
  (a world entry's own variables, [scenes.md](scenes.md)), so world 1 is the wheel: 0.94 m, 69 kg, μ 4
  max-combined, 520 N m, `WheelRoller`. `SphereAgent::spawn_spacing` (template variable, 3 m here, 0 elsewhere)
  spawns world i around (3 i, 0), so the spheres start 3 m apart. `WorldView`'s new `colors` param gives world i's
  bodies the colour `colors[i % n]`. The worlds share nothing but the terrain generator: the spheres pass through
  each other.
- **Steered agent.** `SphereAgent::steered` (template variable `steered`): the goal comes from `steerSphere`
  (a horizontal velocity: its heading and speed, not capped, the command source picks; a null command keeps the
  heading). No goal
  draws, no time limit; a fall (1000 m below the spawn point here) restarts at the spawn point, at rest. The live
  target still turns and accelerates toward the goal at `turn_rate` and `acceleration`, raised from training's
  0.7 rad/s and 0.75 m/s² to 1.5 and 1.5. Over 6 scripted players × 60 s (SR.19, a new command every 1.5 to 4 s with any
  heading change), 1.5 had the smallest error against the command once the target had caught up with it:

  | turn rate, acceleration | 1.2 | 1.5 | 2.0 | 3.0 |
  |---|---|---|---|---|
  | policy: error to the live target (m/s) | 0.22 | 0.21 | 0.25 | 0.27 |
  | policy: error to the command, held ≥ 3 s (m/s) | 0.28 | 0.19 | 0.25 | 0.31 |
  | controller: error to the command, held ≥ 3 s (m/s) | 0.50 | 0.52 | 0.55 | 0.55 |

  At the training rates the target lags the stick (a 180° turn takes 4.5 s); instant targets (rates of 100) gave
  0.32 / 0.40 on 3 players.
- **`RollerPlayerSys`** (`src/view/roller_player.cpp`, host, after `WorldStepper`) commands one world's sphere.
  It is registered twice: `VRRoller` (world 0, before `VRControl`) and `DesktopRoller` (world 1, after
  `PlayerControl`). For the VR player, the left stick, relative to where the head looks, is the command (dead zone
  0.15, full deflection `stick_speed` = 3 m/s, training's top target speed).
- **Lean.** With the stick at rest, once A (right controller) has been pressed: each press of A (its rising edge)
  stores the top of the head in stage space, and the horizontal offset of the top of the head from it is the
  command's direction (turned to the world with the play area's yaw). Its speed is
  `leanSpeed`: 0 within `lean_deadzone` = 1 cm, then `lean_speed · ln(d / 1 cm) / ln(lean_range / 1 cm)`, so each
  doubling of the lean adds the same speed (1 m/s at 2 cm, 2 at 4 cm), and `lean_speed` from `lean_range` on.
  Scene variables `var.lean_speed` (default 3 m/s) and `var.lean_range` (default 0.08 m); the dead zone and the
  head point are `VRRoller` params. The dead zone is there because a log needs a floor, and because standing still
  sways the head by about a centimetre. The top of the head is the head pose's point `head_top` = (0, 0.11, 0.09) m in
  OpenXR view space, 11 cm up and 9 cm back from the eyes. It sits about over the neck's yaw axis, so looking left
  or right barely moves it, while the eyes swing about 9 cm. Nodding still moves it forward. The OpenXR action
  `primary` is new: A or X on Touch, A on Index (`VRInput::Hand::primary`, `primary_pressed`).
- **Above 3 m/s** (`var.lean_speed=4`). Training's targets stop at 3 m/s. The policy still gets to 4 m/s (always
  4 m/s: mean speed 3.94 m/s over held commands, SR.20), but holds commands up to 4 m/s to 0.41 m/s (0.19 up to 3,
  SR.19; the controller manages 0.64). Fine-tuning with `speed_range` up to 4.5 would close the gap.
- **Desktop player** (`DesktopRoller`). The desktop camera is the player's eyes on the wheel, `eye_height` = 1.6 m
  above its lowest point, with or without a VR session. That is inside the wheel (its top is at 1.88 m), whose
  surface is back-face culled from there; `SphereView` hides the rotation dots from a camera inside the sphere. Hold the right mouse button to look
  (`PlayerControl`'s mouse look; its free flight is overwritten). WASD rolls the sphere relative to where the camera
  looks: 2 m/s, 3 with shift. Without a headset the VR player's sphere waits at its spawn point.
- **Standing on the sphere.** The play area and the desktop player follow their sphere between the last two ticks,
  their floor at the sphere's lowest point, smoothed in height at 10/s (it snaps after a jump of more than 2 m).
  `VRRoller` sets `VRRig::stick_locomotion` off, so `VRControl` keeps the right stick turn, the hands and the box
  throw but no longer moves the rig. If the headset is off or in a system menu, the command is zero and the sphere
  stops.
- `SphereView` shows the desktop player's sphere (world 1): its panel and rotation dots, with `camera: false` (it
  leaves the desktop camera to `DesktopRoller`) and `bars: false`, since from the player's eyes the bars are a stripe
  across the view. The panel's Bars checkbox brings them back, and its World slider switches to the VR player's
  sphere.
- The spheres live in their own worlds, so they do not collide with base's boxes (thrown with the right trigger or
  a left click; they are in the host's PhysicsWorld), with the hands or with each other.

Verified headless (SR.17 to SR.21) and on the desktop through captures with scripted WASD, shift and mouse look
(the capture harness's new `"motion": [dx, dy]` input sends relative mouse motion):
`build/artifacts/vr_roller/vr_roller_desktop_first_person.mp4` (one player), `build/artifacts/vr_roller/two/` (the
desktop player looking at the VR player's sphere, then rolling). The stick, the lean and the A button have not yet
been tried in a headset.

## Wheel (`wheel_roll`, the next training)

The same task on a human-scale body: a sphere whose radius is the walker's hip height.

- **Body** (`common/wheel_body.json`, behaviour `WheelRoller`):
  - radius 0.94 m, the H173 walker's hip height (its Hips at 1.03 m × 0.9153);
  - 69 kg, the H173 walker's mass;
  - solid-sphere inertia 2/5 m r² = 24.4 kg m², with the centre of mass at the centre, so at hip height on flat
    ground;
  - μ = 4 (the sphere's friction, max-combined with the terrain's 0.8);
  - `max_torque` 520 N m. This keeps the small sphere's τ / (m g r) = 0.816, so the top acceleration on flat
    ground (5/7 × 0.816 g = 5.7 m/s²) and the steepest slope it can hold (about 55°) are unchanged.
- **Variables.** `sphere_world` takes `behavior`, `radius`, `mass`, `friction`, `friction_combine` and
  `max_torque` as variables. Their defaults are the small sphere's, so `sphere_roll`, the tests and the shipped
  policy are untouched. `wheel_body.json` overrides them for the scenes `wheel_roll` (training, 25 worlds) and
  `wheel_view` (viewer: the follow camera 7 m back and 3.5 m up). Any scene can take the body with
  `var.radius=…` and the other variables.
- **Static friction** (SR.23). `PhysicsWorld::sphereContact` uses Jolt's `CollideShape` to find the deepest point
  of static geometry within the sphere, on a face, an edge or a vertex. Under the proportional controller:
  - the contact carries load (penetrates) on 80% of the ticks; on the rest the sphere sits within 2 mm, held by
    Jolt's speculative contacts;
  - on 98.8% of the loaded ticks the contact point slides less than 5 cm/s, and the 99th percentile of sliding is
    0.07 m/s;
  - the small sphere (geometric μ 0.89) sticks on 80% and sometimes spins in place (p99 13 m/s).

  Sweep over 300 decisions:

  | μ | static share | sliding p99 |
  |---|---|---|
  | 1 | 85% (at 2 mm) | 22 m/s |
  | 2 | 98.1% | 0.19 m/s |
  | 4 | 98.5% | 0.10 m/s |

  Above 4 nothing changes: the remaining slides come with triangle edges and landings, not with the friction
  limit.
- **Baselines** (SR.22, 8 worlds × 900 decisions):
  - proportional controller 0.61, zero torque 0.07 (the small sphere: 0.61 and 0.08), so the scaling holds;
  - the small sphere's policy, run on the wheel without retraining: 0.68 (0.85 on its own body).
- **Training.** `training/wheel_roller_ppo.yaml` uses the small sphere's hyperparameters:

  ```bash
  build/venv/Scripts/python tools/train_sphere.py --scene wheel_roll --run-id wheel_night --max-steps 200000000
  # or from the small sphere's network and normaliser:
  build/venv/Scripts/python tools/train_sphere.py --scene wheel_roll --run-id wheel_night --max-steps 200000000 \
      --init-path build/runs/sphere_mc_night_20261031/SphereRoller/checkpoint.pt
  build/RelWithDebInfo/dynamical.exe scene=wheel_view var.model=latest:build/runs/wheel_night/WheelRoller
  build/venv/Scripts/python tools/sphere_video.py --run wheel_night --seconds 30
  ```

  `--init-path` writes the behaviour's `init_path`. `sphere_video.py` picks `wheel_view` for `WheelRoller`
  checkpoints.

  Smoke run `wheel_smoke_20261101` (16 envs, 1.5M steps, from scratch): mean episode reward 73, 132 and 174 at
  0.5M, 1M and 1.5M, against 75, 122 and 171 for the small sphere's `smoke_mc`. That is 4.8k steps/s (5.4k for the
  small sphere), so 200M steps take about 11.5 h.
- **Run `wheel_50m_20261101`** (50M steps from scratch, 16 envs, 2.2 h at 6.3k steps/s): mean episode reward 533
  at 34M, 547 at 45M, 544 at 50M, above the small sphere's 200M plateau (about 480). The final checkpoint ships as
  `resources/policies/wheel_roller.onnx`. `vr_roller`'s desktop player rolls it, and at a held 3 m/s command it is
  within 0.01 m/s.

## Tests

| ID | Label | Checks |
|---|---|---|
| SR.1 | unit | goal frame: y up, z target, x = up × z, proper rotation, cross products preserved |
| SR.2 | unit | reward formula, vertical velocity ignored |
| SR.3 | unit | heuristic torque direction and clamping |
| SR.4 | scenario | a window's reward uses the observed (decision) target, not the drifting live one (marching cubes) |
| SR.5 | scenario | observations in the goal frame; ground normals against the smooth surface (dot > 0.995); flat ground gives up (height field) |
| SR.6 | scenario | height field: the proportional controller tracks the target (> 0.6), zero torque does not (< 0.4) |
| SR.7 | scenario | episodes end at max_decisions and off the field, truncated, and restart at the next window (height field) |
| SR.8 | scenario | same seed same rollout; another seed another rollout (marching cubes) and another terrain (height field) |
| SR.11 | scenario | marching cubes: spawn on the ground, unit normals, controller beats zero torque by 0.3 |
| SR.12 | scenario | the trainer's RESET staggers the first episodes |
| SR.13 | unit | PolicyRunner equals Python onnxruntime on 100 observations (≤ 1e-5), batched and single |
| SR.14 | unit | a model of another layout, or a missing file, is refused with the reason |
| SR.15 | unit | `latest:` picks the largest step |
| SR.16 | scenario | `Policy` writes the policy's action; the trained policy beats the controller by 0.05 |
| SR.17 | scenario | a steered agent: `steerSphere` goals only, no time limit, restart at rest at the spawn point facing the player's heading |
| SR.18 | unit | `vr_roller`: `VRRoller` (world 0) after `WorldStepper` and before `VRControl`, `DesktopRoller` (world 1) after `PlayerControl`, `Policy@act`, two steered agents spawned 3 m apart, the shipped policy loads, the lean variables reach `VRRoller` |
| SR.19 | scenario | 6 scripted players up to 3 m/s: the shipped policy's errors are below the controller's, and within 0.25 m/s of a held command |
| SR.20 | scenario | up to 4 m/s (`var.lean_speed=4`): the policy's held error below the controller's; always 4 m/s: mean held speed within 10% of 4 |
| SR.21 | unit | `leanSpeed`: 0 in the dead zone, equal steps per doubling, the top speed at the range and beyond, rising |
| SR.22 | scenario | `wheel_roll`: 0.94 m, 69 kg and solid-sphere inertia as Jolt applies them (impulse responses), centre of mass at the centre, μ 4 max-combined, 520 N m, `WheelRoller`; the controller beats zero torque by 0.3 |
| SR.23 | scenario | `wheel_roll`: under the controller the loaded contact slides < 5 cm/s on more than 97% of the ticks, p99 < 0.2 m/s |
| SR.9 | protocol | MockTrainer drives `mode=train`: brain parameters, ids, done routing, rewards in [0, 1], stats channel |
| SR.10 | protocol | `mode=train` refuses a scene without agent worlds |
