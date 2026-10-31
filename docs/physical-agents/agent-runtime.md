# Agent runtime

The task is defined natively in C++ on Jolt state. It reproduces the experiments' **intent and
behaviour**, not Unity's arithmetic. The Unity reference, with file:line sources, is
[unity-analysis.md](unity-analysis.md) ("UA §x"). The trainer contract stays exact:
- behaviour names, observation and action sizes, the sensor name;
- reward and done semantics;
- stat names and env param names.

## Components (agent root entity, next to `ArticulatedBody`)

```cpp
struct Agent        { uint16_t behavior; int32_t group_id = 0; };
struct AgentId      { int32_t value; };                       // stable across episodes
struct ActionBuffer { std::array<float, 120> a{}; bool fresh = false; };
struct HeldDrive    { std::array<JointDrive, 15> joints; };    // stiffness, damping, target rotation, target ω (constraint space)
struct ObsStack     { std::array<float, 138 * 2> v{}; };       // oldest first, zeroed at reset
struct AnchorHistory{ std::array<glm::vec3, 15> prev; bool valid = false; };   // finite-difference velocities
struct RewardWindow { float reward = 0; std::array<float, 16> terms{}; };
struct Episode      { uint64_t tick = 0; uint32_t decisions = 0; bool killed = false, truncated = false, parked = false;
                      std::array<float, 138 * 2> terminal_obs; float terminal_reward = 0; };
struct Task         { glm::vec3 target; float target_speed; float arc_timer_s; float est0; /* push/pull state */ };
struct EnergyState  { /* per joint per window: pos/neg/abs motor work, limit work, vectors τ, ω_rel; whole-body E */ };
struct Recording {};                                           // tag: demo recorder / rig_anim
```

## Layouts

Layouts are versioned and registered by name; the params file selects one.

| Layout | Use |
|---|---|
| **`heidelbot_notoes_2`** | **Trained.** The Jolt-native definition below |
| `heidelbot_notoes_1` | **Test oracle only.** Unity's exact math (non-orthogonal referential, right-side negation, ZXY targets), used to check the rig, frames and features against Unity logs. Never trained |

Both have 138 floats, a stack of 2 and 120 actions. The sensor name the trainer sees stays
`StackingSensor_size2_VectorSensor_size138` (*verify* the exact string with the fork). The layout
version goes into the run metadata and the demo cache key, so `_2` demos and checkpoints never mix
with Unity's.

### Frames

- **Segment forward** `f(s)` = the segment's rest forward axis in world space (Unity's +z, i.e. −z in
  the engine after import).
- **Heading frame** `H(s)`, yaw-only:
  - `x = normalize(horizontal(f(s)))`;
  - `y = world up`;
  - `z = x × y` (right-handed).
- **Intent.** This replaces Unity's non-orthogonal referential (UA §2.2). The command and the
  velocities stay expressed relative to where the body faces. The vertical component is always true
  world vertical, which carries gravity and inclination, as in Unity. Pitch no longer leaks into x.
- **Parent frame per joint** (UA §2.3):
  - Spine: its own frame;
  - UpLegs, Spine2: Spine;
  - Head, Arms: Spine2;
  - Leg: UpLeg; Foot: Leg; ForeArm: Arm; Hand: ForeArm.

### Header (3)

```
tsv  = horizontal(target − Spine.anchor).normalized · target_speed      (0 while dying, 0 when Recording)
v    = horizontal(Spine COM linear velocity)                             (Jolt body velocity: true COM velocity)
d    = tsv − v                                                           (0 when Recording: see GAIL coupling)
est  = max over feet of (Head.anchor.y − Foot.anchor.y)
obs  = [ H(Spine).x · d,  clamp01(1 − est / est0),  H(Spine).z · d ]
```

`est0` comes from the **body file's rest pose**, not the spawn pose. That removes Unity's dependency on
scene placement (UA §3.3).

### Per joint (9 × 15, joint order of the body file)

```
n = R_child · twist_axis        (world)      → H(parent) components
t = R_child · plane_axis        (world)      → H(parent) components
u = (anchor_t − anchor_{t−1}) / h            → H(parent) components    (finite difference, world)
```

- **No right-side negation.** It only flipped signs (UA §2.3).
- **Finite-difference anchor velocity is kept.** It is robust, and identical between kinematic demos
  and dynamic training. `AnchorHistory` is **reset after every pose write** of a reset, which removes
  Unity's 60–120 m/s spike after a random yaw or random init.
- The anchor is the joint pivot, i.e. the child bone origin.

### Actions (8 × 15)

`[s, d, ax, ay, az, ex, ey, ez]` per joint. Gains and targets are held for the whole window.

| Slot | Mapping | Jolt |
|---|---|---|
| s, d | `(a + 1)/2` | `stiffness = rotationSpring · s · factors`, `damping = rotationDamper · d · factors` (Head `springFactor` 0.5); scale re-derived in M2 |
| ax | `twist = lo + (ax + 1)/2 · (hi − lo)`; a = 0 is mid-range | twist part of the target |
| ay, az | `swing_y = ay · Y`, `swing_z = az · Z` | swing part (cone) |
| target | `q = swing(swing_y, swing_z) · twist(twist)`, relative to rest | `SetTargetOrientationCS(q)` |
| ex, ey, ez | `ω = e · maxTargetAngularVelocity` (rad/s, constraint axes) | motor target angular velocity; the sign is defined physically (positive ω turns the child positively about the axis) |

- Initial gains (0.5, 0.5) at reset; targets at rest.
- No clipping in C++; the trainer clips with `clamp(−3, 3)/3`.
- `maxTargetAngularVelocity` is an experiment variable: it flips the sign of net joint work (UA §2.4).
- **Knee rest policy.** The knee's low twist stop is 2° while the rest pose is straight (UA §1.2). The
  body importer bends the rest knee to the stop plus 0.1°, so reset poses are inside the limits. The
  rest pose stays the drive's zero reference.

## Task systems

### Rewards (per decision; UA §3.2, §4.1)

| Term | Definition | Default (flat walker / DeVita) |
|---|---|---|
| first decision | no reward on the first decision of an episode | |
| `root` | `rootSpeed = ((Spine.anchor − at last decision)/T) · normalize(tsv)`; `r = min(rootSpeed, targetSpeed) · w / targetSpeed` (undivided when targetSpeed = 0); not while dying | 0.34 / 0 |
| `energy` | UA §4.1 steps 1–7, fed by the **exact Jolt channel** (motor work per joint per sub-step); `v` from the pelvis | weight 0 / 1; `energyScale` re-derived |
| `life`, `dying`, `head*`, `win` | as UA §3.2 | 0 |
| `collision` | off (weight 0) but `Collision/*` stats kept | |
| `death` | `deathPenalty` on kill | −10 |

- **Scaling.** Rewards are scaled by `rewardFactor` and added only while training.
- **Terminal window.** The terminal window's rewards **are counted**; Unity discards them (UA §3.2).
  This is a declared difference.

### Termination (UA §3.3)

- **Kill:** `est < dyingHeightFactor · est0` (0.7), with `dieOnFall`. `maxDyingFrames` 0 kills on the
  first dying tick. It is a terminal done with the death penalty.
- **Truncation:** `episode_s` (33.3 s) gives an interrupted done (`max_step_reached`), plus the win
  reward if alive.

### Reset (explicit order; UA §3.4)

1. **Target speed**, by an explicit rule:
   - with `continuousSpeed`, uniform in [`targetSpeedMin`, `targetSpeedMax`];
   - else, if randomizing and `randomSpeed`, a pick from `speeds` ({0, 1.36});
   - else `targetSpeed`.

   Unity silently lets a config `targetSpeed` overwrite the draw. Here that is the named rule
   `env_target_speed_overrides_draw` (default **true**, to reproduce existing configs).
2. Episode env params applied (UA §9.3 precedence).
3. Ground: slope rotation and speed compensation (M7).
4. Rest pose, zero velocities, `ResetWarmStart`.
5. Target: if randomizing and `randomTarget`, a random radius of 20–30 m around the head; otherwise
   1000 m straight ahead.
6. If randomizing: random yaw; then random init with probability `randomInitChance` (+2 m, −4 m/s,
   random rotation).
7. **Reset `AnchorHistory`, `ObsStack`, energy, contacts and reward windows**, then run `BodySync` once.

- **Randomizing** = `randomize && training && !Recording`.
- **Arc update:** every `arc_s` (6.67 s; defined in seconds, see [conventions.md](conventions.md#time)),
  the target is re-placed and, unless continuous, the speed is redrawn.

### Disturbances (UA §3.5)

- **Push:** per tick with probability `pushChance`, on a random segment: `AddImpulse` and
  `AddAngularImpulse` of `pushForce · totalMass` with an upward-biased direction. `pushForce` default 5.
- **Pull:** a temporary `SixDOFConstraint` to `Body::sFixedToWorld` with position and orientation
  motors (spring `100·m·f`, damper `10·m·f`, max `1e4·m·f`), lasting up to 2 s.
- Both use the world RNG.

### GAIL coupling (UA §6)

- Demos record header x,z = 0, while the policy sees `d`. Since GAIL sees the whole vector, the
  discriminator **implicitly rewards tracking the velocity command**. This is part of the task, so
  `_2` keeps it.
- Moving the header into a `nogail` sensor would be a new layout version, with new demos and a new
  baseline.

### Stats

Stats keep their names: `Root/*`, `Head/*`, `Death/*`, `Win/*`, `Bot Joint/*` (UA §2.4),
`EnergyProvider/*`, `DeVita/*`, `Slope/*`, `Collision/*`.

- `DeVita/*` follows UA §4.2 on the exact channel. `DeVita/Full GRF/*` is emitted with the same values,
  because on Jolt the exact channel already includes friction.
- Use the ratio of sums.
- **New stats:**
  - `Body/Torso Pitch`, since lean is otherwise unmeasured;
  - `Physics/Foot Penetration`;
  - `Energy/Closure Residual`.

Stats are emitted only while training.

## Outbox

```cpp
struct AgentStep { int32_t id; uint16_t behavior; std::span<const float> obs; float reward; bool done, max_step_reached; int32_t group_id; float group_reward; };
struct StatEntry { std::string key; float value; uint8_t aggregation; };   // 0..3, includes Histogram
struct Outbox    { std::vector<AgentStep> steps; std::vector<StatEntry> stats; };
```

There is one outbox per world. `TrainHost` gathers them in world index order.
