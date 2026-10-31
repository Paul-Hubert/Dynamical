# Unity HumanBot (noToes) — what it computes, for a Jolt-native rebuild

*Survey of 2026-10-31. Read-only analysis of the Unity tree `C:\Users\Paul\Desktop\Dynamical-noToes`,
branch `slope-lean-devita-on-master` @ `815fb4b41`. All paths are relative to that tree.*

**Purpose.** Reproducibility, not compatibility. This doc records what the Unity code actually
computes and why, so the C++ / Jolt implementation can reproduce the **behaviour** of the experiments
with its own clean design.

**Status of the content.**
- **Coverage.** Three follow-up surveys on the same day covered the body (from the prefab YAML),
  energy, DeVita and slope. The remaining **UNVERIFIED** items are listed at the end.
- **Dirty working tree.** `EnergyRewardProvider.cs`, `JointAgent.cs` and `RealizedWork.cs` have
  uncommitted changes (`energyFullGrf`, an indirect internal-energy channel). Two configs are
  untracked: `devita-h173-pn-*-charge-grf-80m.yaml`. None of this is part of the contract until it is
  committed.

---

## 1. Body

### 1.1 Segments, hierarchy, masses

The hierarchy comes from `Assets/AI/HeidelBot/Prefabs/HeidelBot.prefab`. Masses are set in code as
`mass = totalMass · bodyPercentage / 100` (root: `BotAgent.cs:140`; joint bodies: `BotJoint.cs:240`).
The prefab's Rigidbody masses of 1 are overwritten at Initialize.

| # | Body | Parent | Compound colliders merged in | % | kg @ 70 |
|---|---|---|---|---|---|
| 0 | `HeidelBot` (root = pelvis, origin at the feet) | — | `mixamorig:Hips` capsule | 13.66 | 9.562 |
| 1 | LeftUpLeg | root | capsule | 10.5 | 7.35 |
| 2 | LeftLeg | LUpLeg | capsule | 4.75 | 3.325 |
| 3 | LeftFoot | LLeg | child `LeftCollider` box | 1.43 | 1.001 |
| 4 | RightUpLeg | root | capsule | 10.5 | 7.35 |
| 5 | RightLeg | RUpLeg | capsule | 4.75 | 3.325 |
| 6 | RightFoot | RLeg | `RightCollider` box | 1.43 | 1.001 |
| 7 | Spine | root | own capsule + `Spine1` capsule | 13.06 | 9.142 |
| 8 | Spine2 | Spine | own capsule + `Neck`, `LeftShoulder`, `RightShoulder` capsules | 20.1 | 14.07 |
| 9 | LeftArm | Spine2 | capsule | 3.25 | 2.275 |
| 10 | LeftForeArm | LArm | capsule | 1.87 | 1.309 |
| 11 | LeftHand | LForeArm | box | 0.65 | 0.455 |
| 12 | Head | Spine2 | capsule (`Eye` capsules are triggers: exclude) | 8.26 | 5.782 |
| 13 | RightArm | Spine2 | capsule | 3.25 | 2.275 |
| 14 | RightForeArm | RArm | capsule | 1.87 | 1.309 |
| 15 | RightHand | RForeArm | box | 0.65 | 0.455 |

- **The percentages sum to 99.98** (fixed on 2026-08-20 from 117.98; memory `body-anthropometry.md`).
  DeVita overrides `totalMass` to 69 in every scene.
- **No code writes `centerOfMass` or `inertiaTensor`.** PhysX derives them from the colliders and
  rescales them by mass.
- **For Jolt, choose the inertia source explicitly**, as a field of the body version:
  - `physx_readback`: the values PhysX derived, read back in play mode. This reproduces the Unity body.
  - `anthropometric`: the de Leva / Plagenhoef tables.

  Either way, mass properties are **provided** to Jolt, never computed from shapes.
- **UP §3.1's uniform 3.889 kg is the gladiator body**, not this one.

### 1.2 Rest pose, shapes, joints (parsed from the prefab YAML)

**Source.** `HeidelBot.prefab` is a standalone prefab with 16 Rigidbodies and 15 ConfigurableJoints.
All six `DeVitaArena*.unity` scenes instantiate it directly. `HeidelBotBase.prefab` is an unused
variant.

**Rest pose.** World bone origins, prefab at the origin, scale 1, quaternions (x, y, z, w):

| Body | World origin | World rotation |
|---|---|---|
| root (pelvis) | (0, 0, 0): floor level | identity |
| Spine | (0.0001, 1.1379, −0.0124) | identity |
| Spine2 | (0.0001, 1.4062, −0.0124) | identity |
| Head | (0.0001, 1.6301, 0.0154) | (0.035, 0, 0, 0.99939) |
| L/R UpLeg | (∓0.1088, 0.9723, −0.0002) | (0, 0, 1, 0) |
| L/R Leg | (∓0.1088, 0.5207, −0.0002) | (0, 0, 1, 0) |
| L/R Foot | (∓0.1088, 0.1152, −0.0002) | L (−0.01767, 0.47793, 0.87644, 0.05581), R mirrored |
| L/R Arm | (∓0.2031, 1.4914, ≈ −0.003) | L (0.85014, −0.11816, 0.50569, 0.08709), R mirrored |
| L/R ForeArm | (∓0.276, 1.2497, ≈ 0.004) | L (0.85753, −0.03670, 0.49510, 0.13483), R mirrored |
| L/R Hand | (∓0.3228, 1.021, ≈ 0.05) | same as the forearm |

- **Joint pivot = child body origin.** `m_Anchor` is (0, 0, 0) everywhere. The connected anchors are
  auto-configured at runtime; the stored values are stale.
- **The pelvis capsule sits on the unbodied Hips transform at y ≈ 1.03.** Since the centre of mass is
  automatic, the root's centre of mass lands there.
- **The feet are scaled 1.0517.** The toe transforms remain for skinning only.

**Colliders.** Capsule `dir` 0 = X, 1 = Y.
- **Compound bodies:**
  - root: Hips capsule (r 0.1136, h 0.355, X);
  - Spine: own capsule (r 0.1204, h 0.344, X) + Spine1 (r 0.1286, h 0.353, X);
  - Spine2: own capsule (r 0.1310, h 0.404, X) + both shoulders (r 0.072, h 0.252, Y) + neck
    (r 0.0785, h 0.157, Y).
- **Head:** r 0.118, h 0.299 (Y). The two Eye capsules are triggers.
- **Legs:**
  - UpLeg: r 0.085, h 0.6215;
  - Leg: r 0.075, h 0.5556;
  - foot boxes on child objects: L (0.1032, 0.2648, 0.0687), R (0.1045, 0.2682, 0.0708), before the
    1.0517 scale. They are rotated about 32° about X.
- **Arms are asymmetric** (keep as is):

  | Segment | Left | Right |
  |---|---|---|
  | Arm capsule radius (h 0.363) | 0.0736 | 0.0817 |
  | ForeArm capsule radius (h 0.314) | 0.0609 | 0.0671 |
  | Hand box | (0.132, 0.213, 0.080) | (0.128, 0.190, 0.080) |

- Full centres and rotations are in the prefab at the lines listed in the survey; the exporter must
  read them from the prefab rather than from this table.

**Material.** `HeidelBot.physicMaterial`, on every non-trigger collider: friction 1 / 1, bounce 0,
friction combine Maximum, bounce combine Minimum. The floor is a 150 × 10 × 150 `ArenaFloor`, top at
y = 0, with the same material.

**Joints.** Limits are in degrees. Y = swing about `secondaryAxis`, Z = swing about axis × secondary.
All joints:
- linear motion locked, angular limited;
- limit springs, bounce and contactDistance all 0;
- projection off, preprocessing off, no collision between the connected bodies;
- Slerp drive.

| Joint | Parent | axis / secondary | lowX | highX | Y | Z |
|---|---|---|---|---|---|---|
| Spine | root | (1,0,0) / (0,1,0) | −26.178 | 24.260 | 26.942 | 20 |
| Spine2 | Spine | same | −26.178 | 24.260 | 26.942 | 20 |
| Head | Spine2 | same | −75 | 75 | 90 | 55.2 |
| UpLeg (hip) | root | same | −92.243 | 74.885 | 36.234 | 36.503 |
| Leg (knee) | UpLeg | same | **2** | 132.620 | **15** | 13 |
| Foot (ankle) | Leg | same | **−38** | 61.130 | 20.617 | 30.751 |
| Arm | Spine2 | same | −82.437 | 156.476 | 174.840 | 112.721 |
| ForeArm | Arm | **L (0,0,1), R (0,0,−1)** / (0,1,0) | −11.677 | 121.408 | 83.451 | 11 |
| Hand | ForeArm | (1,0,0) / (0,1,0) | −79.785 | 67.065 | 7 | 36.411 |

- **The knee low-X stop is 2° and the rest pose is straight**, so the knee snaps onto the stop on the
  first step.
- **The arm swing limits (174.8° / 112.7°) exceed what a cone constraint can represent.** Jolt's swing
  cone half-angles must stay below 180°, and the elliptical-cone behaviour near 180° needs a test.
- **Dead prefab values:** the drive and break values in the prefab are overwritten at Configure.
  Limits are never written at runtime.

**Gladiator differences** (for reference only):
- toe bodies;
- short foot boxes;
- uniform 5.5556 % masses;
- ankle low-X −30;
- knee low-X 0 and Y 10.

**Rigidbodies.** All 16 identical:
- linear damping 0, angular damping 0.05;
- ContinuousDynamic;
- `maxAngularVelocity` 20, set in code.

Solver iterations come from the project default (20 / 20).

### 1.3 Drives

| Item | Value | Source |
|---|---|---|
| Mode | SLERP | `BotJoint.cs:244` |
| Spring | `rotationSpring · s · torqueFactor(global) · torqueFactor(joint) · springFactor` | `BotJoint.cs:669` |
| Damper | `rotationDamper · d · torqueFactor · torqueFactor · damperFactor` | `BotJoint.cs:671` |
| Max force | `maxTorque · torqueFactors` = 200000 | `BotJoint.cs:672` |
| `rotationSpring` / `rotationDamper` | 2983.68 / 336 | prefab |
| Factors | all 1, except Head `springFactor` 0.5 | prefab |
| Initial gains | (0.5, 0.5) | `BotJoint.cs:246` |
| `enablePreprocessing` | false | `BotJoint.cs:200` |
| Max angular velocity | 20 rad/s per body (prefab); project default 10 | `BotJoint.cs:242`, `BotAgent.cs:141`, `DynamicsManager.asset:38` |
| CCD | `ContinuousDynamic` on joint bodies; root UNVERIFIED | `BotJoint.cs:186` |
| Break force | infinite while training | `BotAgent.cs:128-132` |

**Drive zero = the rest pose:** `startLocalRotation` is captured at Initialize and OnEnable
(`BotJoint.cs:205`, `994`).

### 1.4 Joint order (observation and action order)

`JointAgent.cs:444-481`: depth-first hierarchy order, regrouped CENTER → RIGHT → LEFT.

**Spine, Spine2, Head, RUpLeg, RLeg, RFoot, RArm, RForeArm, RHand, LUpLeg, LLeg, LFoot, LArm, LForeArm,
LHand** (15).

The C++ body file lists this order explicitly instead of deriving it.

---

## 2. Observations (138 × 2) and actions (120)

### 2.1 Layout flags (prefab parameter block, ~l.6007-6100; behaviour l.5978-5992)

- **Brain:** `VectorObservationSize 138`, `NumStackedVectorObservations 2`, 120 continuous actions,
  `BehaviorName HeidelBotBehavior`, `DeterministicInference 0`.
- **Control:** `decisionPeriod 2` (30 Hz control at 60 Hz physics).
- **Flags that are on:** `springControl 1`, `angularVelocityControl 1`, `jointSpace 1`,
  `speedDiffZeroRecording 1`.
- **Flags that are off:** `relativeVelocity`, `normalizedVelocity`, `positionObs`,
  `angularVelocityObs`, `collisionObs`, `targetSpeedObs`, `dyingObs`, `speedDiffZero`,
  `normalizeSpeedDiff`.
- **Body roles:** `root` = Spine, `head` = Head, `feet` = [LeftFoot, RightFoot].
- **One sensor only** on the standard line: the stacked vector. GAIL sees all of it, because the fork
  drops only sensors named `*nogail*` (`gail_reward_provider.py:116-126`). Only
  `DeVitaArenaH173NogailFeet` adds a second sensor.

### 2.2 Header (3 floats; `HumanBotAgent.cs:260-293`, `321-331`)

| Element | Math | Verdict |
|---|---|---|
| Command | `tsv = horizontal(target − Spine.pos).normalized · targetSpeed`; 0 while dying | Essential |
| Velocity | `horizontal(Spine.Rigidbody.linearVelocity)`. This is the PhysX COM velocity, which carries a gravity-sized bias on jointed bodies (memory `rigidbody-linearvelocity-biased-on-jointed-bodies.md`) | Artefact: use the true COM velocity |
| `diff` | `tsv − v`, unnormalised; set to 0 when recording | Essential, and coupled to GAIL (§6) |
| `diff.y` | `clamp01(1 − est/est0)`, with `est = max over feet of (head.y − foot.y)` on bone origins. `p.height` cancels out | Essential: uprightness |
| Frame | `UnorthogonalReferential(Spine)` = `(dot(fwd, d), dot(up_world, d), dot(normalize(cross(fwd, up)), d))`. Not orthogonal when the spine pitches: x leaks pitch and shrinks by cos(pitch) | Artefact. Intent: a heading frame |

### 2.3 Per joint (9 × 15; `BotJoint.cs:296-372`, `888-892`)

| Field | Math |
|---|---|
| normal (3) | `R_child · axis`, negated on RIGHT joints, projected in the parent frame |
| tangent (3) | the same with `secondaryAxis` |
| velocity (3) | `(bone origin_t − bone origin_{t−1}) / dt`: world velocity of the joint anchor, projected in the parent frame (`relativeVelocity` 0) |

**Frames** (`jointSpace 1`):

| Joint | Frame |
|---|---|
| Spine | its own frame |
| UpLegs, Spine2 | Spine |
| Head, Arms | Spine2 |
| Leg | UpLeg |
| Foot | Leg |
| ForeArm | Arm |
| Hand | ForeArm |

y is always the world-vertical component, so `n.y` and `t.y` carry each segment's absolute
inclination. That is the only gravity information in the vector. The pelvis is never observed
directly.

| Artefact | Weight | Clean replacement |
|---|---|---|
| Non-orthogonal frame | low | Heading frame of the parent (yaw-only, world up kept) |
| Right-side negation | nil (fixed sign) | Drop it |
| Finite-difference anchor velocity | medium (robust) | Keep. Equivalent: the Jolt point velocity at the anchor |
| **Velocity spike after a reset teleport** | hazard | `Restart` sets `lastPosition` (`BotJoint.cs:292`) **before** random yaw and random init move the body (`BotAgent.cs:156-163`), so the first velocity is 60–120 m/s. Reset the finite-difference history after every pose write |
| Stack of 2, oldest first, zeroed at reset (`Agent.cs:628`) | essential (trainer, GAIL) | Keep |

### 2.4 Actions (8 × 15; `BotJoint.cs:402-550`, `577-617`, `666-678`)

| Slot | Math | Jolt-native |
|---|---|---|
| s, d | `(a+1)/2`, held until the next decision | Motor stiffness and damping; re-derive the scale |
| ax | `target.x = ax·(hi − lo)/2 + (hi + lo)/2` (deg): 0 = middle of the twist range | Same mapping. Half-bent at a = 0 is part of the task |
| ay, az | `ay · swingY`, `az · swingZ` | Same, on the swing cone |
| Rotation | `Quaternion.Euler(ZXY)` → `joint.targetRotation`, inverse joint-space convention relative to `startLocalRotation`; rewritten every tick | `q = swing(ay, az) · twist(ax)` relative to rest, in constraint space; no Euler |
| ex, ey, ez | Reduces exactly to `ω = e · maxTargetAngularVelocity` (rad/s, joint axes). The SLERP drive applies a minus sign | Target angular velocity in constraint space, sign defined sanely. `maxTargetAngularVelocity` (prefab 1.0472, configs 7 or 14) flips the sign of net joint work (memory `devita-angvel-net-work.md`) |
| Clipping | none in C#; trainer `clamp(−3, 3)/3` | same |

Stats per decision and joint: `Bot Joint/Spring Factor`, `Damper Factor`, `Spring`, `Damper`,
`Angular Velocity Factor`, `Action <part>`, `Target Angular Velocity [<part>]`,
`Joint Angular Velocity [<part>]` (`:443-453`, `:495`, `:511-548`).

**SLERP drive intent:**
- an isotropic spring–damper on the quaternion error;
- force mode (torque not scaled by inertia);
- damping on `ω_rel − ω_target`.

---

## 3. Task (flat GAIL walker)

### 3.1 Ticks and decisions

- `RequestDecision` when `academyStep % decisionPeriod == decisionFrame`, with `decisionFrame` 0 when
  training (`BotAgent.cs:196-202`, `133-134`). **One global phase, never reset per episode.**
- Academy order: `AgentPreStep` → `SendState` (obs + reward) → `DecideAction` → `AgentAct`
  (`Academy.cs:568-604`). Observation and action of a decision happen on the same tick.
- Academy and agent `FixedUpdate`s both run at order 0, so their relative order is undefined.
- Fixed script orders: `EnergyRewardProvider` / `LegacyRewardProvider` 100, `DeVitaStepStat` 210,
  `SlopeGround` −100, `PhysicsRate` −32000.

### 3.2 Rewards (`HumanBotAgent.CalculateRewards`, `:137-258`; prefab values)

| Term | Math | Value |
|---|---|---|
| First decision | no reward | |
| Life / dying | `dying ? dyingReward : lifeReward` | 0 / 0 |
| Root | `rootVel = (Spine.pos − rootPos_lastDecision)/(dt · period)`; `rootSpeed = rootVel · normalize(tsv)`; `r = min(rootSpeed, targetSpeed) · w / targetSpeed`, undivided when targetSpeed = 0. Only when not dying | **w = 0.34** |
| Head spring / damper | only while dying | 0 / 0 |
| Energy | `EnergyRewardProvider` (order 100) | UNVERIFIED |
| Collision | `−penalty · factor · \|relVel_COM\| · mass` per contact stay; feet factor 0 (`BotJoint.cs:910-922`, `HumanBotAgent.cs:68-74`) | penalty 0; stats still emitted |
| Scale | × `rewardFactor` (1), only while training (`BotAgent.cs:229-235`) | |
| Terminal window | Snapshotted before the terminal `CollectObservations` (`Agent.cs:604` vs `613`), so it is discarded | count it in C++ |

### 3.3 Termination

- **Kill:** `est < 0.7 · est0` on bone origins (`:511-540`), with `dieOnFall 1` and `maxDyingFrames 0`.
  It pays `deathPenalty` −10 and is a terminal done.
- **`est0` comes from the spawn pose at Initialize** (`:61`).
- **Truncation:** `frameCounter > maxFrames`, 2000 ticks (physics-rate scaled) → interrupted
  (`BotAgent.cs:176-187`). `winReward` 0.

### 3.4 Reset order (`HumanBotAgent.cs:84-96` → `BotAgent.cs:148-174` → `JointAgent.cs:167-182`)

1. `RandomSpeed`: uniform in [`targetSpeedMin`, `targetSpeedMax`] when `continuousSpeed`; otherwise
   from {0, 1.36} if randomizing and `randomSpeed`.
2. Destroy the pull joint; reset counters.
3. `UpdateParameters`: env params, only when `EnvParams.HasSource`. **A config `targetSpeed`
   overwrites the draw** (`BotAgent.cs:289`).
4. Environment `OnEpisodeBegin` (SlopeGround and similar).
5. `Restart`: rest snapshot, zero velocities, finite-difference history.
6. `SetTarget`: a random radius of 20–30 m around the head if randomizing and `randomTarget`;
   otherwise head + 1000 · world +Z.
7. If randomizing: random yaw (with `randomTarget`); with probability `randomInitChance` 0.2, random
   init: +2 m, root velocity −4 m/s, random rotation.
8. Freeze flags, energy reset, `UpdateObjective`.

- **`Randomizing()`** = `env("randomize") > 0 && training && !recording && !animating`
  (`BotAgent.cs:390-399`).
- **`ArcUpdate`:** every 400 ticks (not physics-rate scaled), `SetTarget` again and `RandomSpeed` again
  unless continuous.

### 3.5 Push, pull (`HumanBotAgent.cs:398-459`)

- **Push:** per tick, probability `pushChance` 0.001. On a random body (15 joint bodies + root), an
  impulse `insideUnitSphere · pushForce · totalMass` with y = |y|, plus an angular impulse of the same
  scale. `pushForce` **5** in the prefab (the code default is 1).
- **Pull:** a temporary 6-DOF joint to the world with drives of spring `100·m·f`, damper `10·m·f` and
  max `1e4·m·f`; random rotation, offset ≤ 0.2 m, lifetime random ≤ 2 s.
- **RNG:** one shared stream per process, seeded with `seed + worker_id`. Per-world streams are fine.

### 3.6 DeVita, slope, leans

**Scenes:** `DeVitaArena`, `DeVitaArenaH173` and its variants `Up10Leans`, `Dn10Leans`, `Ladder`,
`NogailFeet`. All of them:
- override `totalMass` to 69;
- have only the 150 m ArenaFloor.

**H173 is a different body.**
- The H173 scenes scale the rig root by **0.9153439** and set `height 1.73`. Colliders and offsets
  scale with it; masses stay at 69 kg.
- Their configs rescale the gains: `rotationSpring 2731.0933` (2983.68·s), `rotationDamper 294.2495`
  (336·s^1.5), `maxAngularVelocity 20.9044` (20·s^−0.5). `maxTargetAngularVelocity` stays 1.0472.
- For Jolt, this is a **separate exported body file** (`heidelbot_notoes_h173`), with its gains coming
  from the params file.

**SlopeGround** (`S/SlopeGround.cs`):
- It rotates the floor about its origin, about the medio-lateral axis.
- It drops the floor by `footReach·tan|θ| + 0.01`.
- With `slopeCompensateSpeed`, it commands `targetSpeed = base · cos θ`.
- It holds the plane level only until the first episode (`:130`).
- Env params: `slopeAngle`, `slopeRandom`, `slopeIncludeLevel`, `slopeSign`, `slopeCompensateSpeed`,
  `slopeFootReach`, `slopeLiftMargin`.
- Stats: `Slope/Angle`, `Slope/Ascent Share`, `Slope/Descent Share`.

**The lean line trains on the ramp from the first episode.** The four lean configs train from scratch
with `slopeAngle 10`, `slopeSign ±1`, `slopeRandom 0` and `slopeCompensateSpeed 0`. "Lean" exists only
in the demos: 10 clips from −4 to +14° of torso inclination (`SlopeGaitLeanSet.cs`). Nothing observes,
rewards or measures it, so add a torso-pitch stat.

---

## 4. Energy

All channels are computed once per physics tick in `JointAgent.FixedUpdate`, for the step just
finished (`S/JointAgent.cs:184-267`). The provider sums them over a decision and divides by T = 1/30 s,
so the `EnergyProvider/*` energies are watts.

| Channel | Computation | Status |
|---|---|---|
| `energySource` 0 (code default) | Analytic PD torque · ω_rel · dt (`BotJoint.cs:767-841`) | Wrong by 10²–10³× (memory `energyproof-verdict.md`). Retire it |
| 1 (whole body) | d(Σ KE + PE) split by sign (`JointAgent.cs:236-251`) | Correct as a net channel |
| 2 (realized; **used by the DeVita configs**) | `tauJ` = total angular constraint couple on the distal body (drive + limits, i.e. DeVita's net joint moment) · midpoint ω_rel, split by sign per joint per tick (`S/RealizedWork.cs:141-307`) | Biased: normal-only contact, TGS split, 60 Hz midpoint |
| `energyFullGrf` (`*-grf-80m` configs only; uncommitted) | Friction impulse recovered from Newton's law (`S/RealizedWork.cs:276-299`) | DeVita ratio 0.551 → 0.468 |
| `energyChargeResidual` (source 2 only) | Adds `max(0, slip + proj)` to the positive energy (`H/EnergyRewardProvider.cs:221-227`) | Closes PhysX books; no equivalent on Jolt |
| Closure (`S/EnergyClosure.cs`) | dE, projection and slip work, per-axis split, limit engagement, residuals | Diagnostics |
| Ratio estimator | Ratio of sums (memory `devita-ratio-estimator.md`) | Keep |

**Gravity is −10 in Unity's energy books**: the whole-body energy and the stance threshold use g = 10.
D6 (−9.81) changes those constants.

### 4.1 Energy reward (`H/EnergyRewardProvider.cs:269-366`, per decision, T = 1/30 s)

1. `P± = Σ` of the source's positive / negative work over the decision, divided by T.
2. `E = posMult·P+ + negMult·P−`.
3. `v_raw` = Hips horizontal displacement over T, projected onto the direction to the target.
4. `v_s` = EMA of `v_raw`, seeded on the first decision, with `α = 1 − exp(−T/τ)`.
5. `CoT = E / clamp(v_s, floor, max(floor, targetSpeed))`, for the whole body (not per kg).
6. `gate = exp(−(v_s − targetSpeed)² / 2σ²)`.
7. `r = gate · exp(−CoT/energyScale) · energyBonusWeight`. It is added always, including while dying.

| Param | Code default | Lean configs |
|---|---|---|
| `energyBonusWeight` | 0.5 | 1 |
| `energyScale` | 30000 | 587 (equal) / 1803.2275 (Margaria) |
| `energySpeedEdge` σ | 0.35 | 0.35 |
| `energyMinSpeedFloor` | 0.5 | 0.5 |
| `energyVelocitySmoothingTau` τ | 0.2 | 0.3 |
| `positiveEnergyMultiplier` / `negativeEnergyMultiplier` | 1 / 1 | 1 / 1 or 4.0 / 0.8333 |
| `energySource` / `energyClosure` / `energyChargeResidual` | 0 / 1 / 0 | 2 / 1 / 1 |

- Also accepted, with code defaults only: `energyFullGrf`, `energyRewardStyle`, `statsSettleSeconds` 2,
  `useTorqueNotEnergy`, `energyLimitEpsDeg` 0.5, `energyNormDivisor` 46700, `energyNormOffset` 1.25.
- `energyMinSpeed` and `energyMaxSpeed` are read but unused.
- `rootReward` is 0 in the DeVita configs.

### 4.2 `DeVitaStepStat` (`H/DeVitaStepStat.cs`, order 210, training only)

- **Joints:** hip = UpLeg, knee = Leg, ankle = Foot, per side. Only the foot has contact.
- **Sagittal axis:** `ml = normalize(up × horizontal direction to the target)`, in world space.
- **Stance:**
  - a limb is loaded when its normal contact force is at least 0.05·m·g (34.5 N);
  - debounce of 3 ticks; the window runs from the first to the last loaded tick;
  - stance length must be 0.15–2 s;
  - stances starting in the first 2 s of an episode are dropped.
- **Work** per joint per tick = `(τ·ml)(ω·ml)·dt`, split by sign before summing.
- **Step length** is measured between contralateral onsets and accepted in 0.2–1.5 m.
- **`DeVita/NegPos ratio`** = ratio of sums, needs at least 4 stances, and is emitted at the next
  episode start, so the last episode is lost. Key lists are at `:610-746`.

**The Jolt channel must output, per sub-step and summed per tick:**
- per joint, signed actuator power `τ_motor · ω_rel`, keeping the **vectors** τ and ω_rel so the work
  can be re-projected sagittally;
- positive and negative work accumulated per sub-step;
- limit-constraint work separately (≈ 0);
- point-constraint work (≈ 0, as a check);
- per-foot contact impulse with friction, and the contact point;
- whole-body ΔE closure: Σ P_j + contact work + gravity ≈ ΔE.

**DeVita stats** = the stance-only, sagittal, one-limb protocol with Full-GRF semantics. Keep the names
`EnergyProvider/*`, `DeVita/*`, `DeVita/Full GRF/*`. Re-derive `energyScale` 6500, the Margaria
multipliers 4.0 / 0.8333 and the gate widths.

---

## 5. Physics settings

| Setting | Unity | Class |
|---|---|---|
| dt | 2351990/141120000 s ≈ 1/60; `physicsRate` ∈ {60, 120, 240, 480} scales the decision period and `maxFrames` (`BotAgent.cs:99-117`) | **Reproduce the intent:** 30 Hz control, 33.3 s episodes |
| Gravity | −10 | D6: −9.81 |
| Solver | TGS 20 / 20, adaptive force on | Tune fresh |
| Friction | μ 1 / 1, combine Max, patch friction | Reproduce: μ = 1, max combine |
| Contact offset 0.04, bounce threshold 1, depenetration 20 | | Tune fresh |
| Max angular velocity | 20 rad/s | Reproduce explicitly, or remove and log |
| Angular damping | 0.05 | Intent only. 0 if energy closure needs it (part of the body version) |
| CCD | ContinuousDynamic | `Discrete` for agents (exact energy and contact books); `LinearCast` stays for `base` rigid bodies |
| Sleep | 0.005 | Irrelevant: sleeping off |
| Self-collision | off (`JointAgent.cs:421-424`) | Reproduce: all pairs filtered |

---

## 6. GAIL demos (`ImitationRecorder.cs:21-118`)

**Recording:**
- A clone plays the Mecanim controller. The agent's bodies are kinematic and follow by name through
  `MovePosition` / `MoveRotation`, so **the pose lands one tick late**.
- No limits are enforced, so the demos exceed the stops.
- The recorder forces `training = true`, `maxFrames = 1000`, `dieOnFall = false`, `torqueFactor 0`.
  It also adds a random z offset in [−1, 0] per episode.
- **Observations:** header x,z = 0 (`speedDiffZeroRecording`); y = head error; joint velocities from
  the finite difference of the kinematic bodies.
- **Actions** are zeros (`Heuristic` is empty, `BotAgent.cs:224-227`).

**The header couples to GAIL.**
- Demos carry header x,z = 0, while the policy carries `tsv − v`, and the discriminator sees both.
- So **GAIL implicitly rewards tracking the velocity command** (standing still when the target speed
  is 0), on top of `rootReward`.
- Any change to the header changes this implied reward. The clean option is a `nogail` header sensor,
  which costs new demos and a baseline.

The recorded root takes root motion while the unbodied Hips bone stays at rest, so pelvis motion
is folded into the hip and spine angles.

**Clips (UNVERIFIED):**
- flat: `BasicMotions@Walk`;
- H173: `Walk1p500mps-h173` with Foot IK;
- lean sets from SlopeGait.

---

## 7. Env param names

| Owner | Names |
|---|---|
| `BotAgent.cs:264-379` | `rotationSpring`, `rotationDamper`, `maxTorque`, `torqueFactor`, `maxAngularVelocity`, `angularVelocityFactor`, `maxTargetAngularVelocity`, `totalMass`, `targetSpeed`, `pushing`, `pushChance`, `pushForce`, `pulling`, `pullChance`, `pullForce`, `collisionPenalty`, `rewardFactor`, `winDistance`, `etaPositiveWork`, `etaNegativeWork`, `randomTarget`, `randomSpeed`, `randomInit`, `randomInitChance`, `selfColliding`, `relativeVelocity`, `freezeJointActions`, `pinFrozenJointDrive` |
| read directly | `randomize`, `continuousSpeed`, `targetSpeedMin`, `targetSpeedMax` |
| `HumanBotAgent.cs:595-628` | `rootReward`, `headSpringReward`, `headDamperReward`, `winReward`, `deathPenalty`, `dyingHeightFactor`, `maxDyingFrames`, `lifeReward`, `dyingReward`, `normalizeSpeedDiff`, `arcFrames`, `dieOnFall`, `resetOnAlive` |
| elsewhere (UNVERIFIED lists) | `physicsRate`; energy provider (`energySource`, `energyScale`, `energyBonusWeight`, positive/negative multipliers, `energyChargeResidual`, `energyFullGrf`); SlopeGround names |

Behaviour names: `HeidelBotBehavior` (flat walker, `Training.unity`); `AnySpeedBotBehavior` (DeVita scenes, scene override).

---

## 8. Reproducibility hazards

1. **Code defaults ≠ prefab values:**

   | Field | Code | Prefab |
   |---|---|---|
   | `normalizeSpeedDiff` | true | 0 |
   | `relativeVelocity` | true | 0 |
   | `normalizedVelocity` | true | 0 |
   | `targetSpeedObs`, `dyingObs` | true | 0 |
   | `pushForce` | 1 | 5 |
   | `decisionPeriod` | 5 | 2 |
   | `maxAngularVelocity` | 10 | 20 |

   Export resolved values only.
2. `p.height` cancels in both the kill test and the head error.
3. Velocity spike after random yaw or random init.
4. Knee rest pose 2° outside its stop; limits changed 2026-10-20.
5. Global decision phase; `arcFrames` not scaled by physics rate.
6. Terminal-window reward discarded.
7. Header uses the biased `linearVelocity`; the reward uses Spine bone-origin displacement.
8. A config `targetSpeed` overrides `RandomSpeed`.
9. Demos: one-tick lag, limits exceeded, header zeroed, re-recorded at every launch.
10. Uncommitted energy changes.
11. **Where UP is wrong or gladiator-only for noToes:**
    - masses;
    - joint order and count;
    - obs and action sizes;
    - `pushForce` 5;
    - `decisionPeriod` 2 is missing from UP;
    - the ω mapping is linear;
    - UP omits adaptive force.

## 9. Scenes, configs, demos, launch

### 9.1 The two lines

| | Flat GAIL walker | DeVita noToes line |
|---|---|---|
| Scene | `Training.unity` | `DeVitaArenaH173.unity` (+ variants) |
| Behaviour | `HeidelBotBehavior` | `AnySpeedBotBehavior` (scene override, l.661) |
| Body | 1.89 m, 70 kg | 1.73 m (scale 0.9153439), 69 kg |
| Ground | shared FlatFloor (≈ 100 km, top y = 0) | 150 m ArenaFloor + SlopeGround (angle 0 in the flat configs) |
| Scene overrides | `energyBonusWeight 0`, `randomSpeed 1`, `randomizing 1`, `randomTarget 1` (one orphaned override is inert) | `rootReward 0`, `height 1.73`, `totalMass 69`; `DeVitaStepStat` added |
| Config | `default-training.yaml`: lr 3e-4, ε 0.2, GAIL strength 1, 40M steps, seed 42 | `devita-h173-*`: batch 5000, buffer 50000, lr 1e-4, β 4e-5, ε 0.15, 512×3, γ 0.95, GAIL 0.5, `use_actions false`, `threaded false`; env params in §4.1 plus `targetSpeed 1.5`, stature-rescaled gains |
| Demo | `BasicMotions@Walk` × 2 at state speed 0.67, 3000 steps (6 episodes of 500 decisions) | `Walk1p500mps-h173` (BasicMotions Walk01 at speed 0.807), 10000 steps |

- **No YAML declares behaviour blocks** for these lines. Both behaviours use `default_settings`; the
  observation size comes from the env's spec.
- **Arena replication.** `TrainingScene.prefab` carries a `PrefabGrid` of 5 × 5 spaced 350 m: 25
  agents per process. The grid is off in the editor and during demo recording.
- **Physics rate.** There is no `PhysicsRate` component in the scenes, so the rate is 60 Hz unless the
  `physicsRate` env param sets it (one config uses 240). `PhysicsRate` scales `decisionPeriod` and
  `maxFrames` by an integer factor and logs `PHYSICSRATE|APPLIED/SCALED`.
- **Material enum.** Bounce combine 2 is Minimum in Unity's enum (Average 0, Multiply 1, Minimum 2,
  Maximum 3). One survey called it Multiply; bounciness is 0, so it does not matter.

### 9.2 Demo recording (`AutoDemonstrationRecorder` → `ImitationRecorder`)

**Pipeline.** `quick-train.ps1` wipes `Demonstrations/` and runs the Editor in batch mode on every
launch. There is no trainer and no env params: scene values only.

**Recorder setup.**
- A full HeidelBot clone plays the controller with root motion (Animator `animate physics`), starting
  at the world origin.
- The recorded agent goes to the `NonCollide` layer, kinematic, with projection off and
  `torqueFactor 0`.
- It sets `maxFrames 1000` (unscaled), `dieOnFall false` and `recording`, plus a random z shift.
- `Time.timeScale 20`; the fixed step is unchanged.

**Per tick.** Each body is matched **by name** and moved with `MovePosition` / `MoveRotation` to the
clone's world pose. `CopyAction` writes joint targets that no observation reads. Velocities are never
set: whether PhysX reports kinematic `linearVelocity` is UNVERIFIED, and it matters only for the header
velocity, which is zeroed while recording anyway.

**Steps.** One demo step is one decision; recording stops at `NumStepsToRecord`. The output is
`Demonstrations/HeidelBot.demo`, and configs read the whole folder.

### 9.3 Env params (complete union; gates)

- **HS (`EnvParams.HasSource`):** BotAgent and HumanBotAgent names (§7), read every episode in
  `UpdateParameters`.
- **RFE:** energy provider names (§4.1); legacy reward names (`legacy*`, inert, `legacyRewards 0`).
- **SlopeGround names:** read at every episode begin, ungated.
- **Read once at Awake:** `physicsRate`.
- **Sent but inert:** `depth`, `energyPenalty`. `seed` is not an env param; the seed is `env_settings`
  seed + worker id.
- **Bools** are float > 0.
- **Order:** `RandomSpeed` → `UpdateParameters` (re-pins `targetSpeed`) → SlopeGround compensates
  `targetSpeed *= cos θ`.

**`EnvParams` precedence** (`EnvParams.cs:110-143`): `-set:` > live trainer > `-envParams=` file >
scene. A missing or malformed `-envParams` file is fatal.

**Dump** (`envparams.txt`):
- rewritten atomically on every change;
- header `# run=<dir> behavior=<name> written=<TotalStepCount>`, then `name value` lines sorted
  ordinally, floats with "R" in the invariant culture;
- `-gitSha` is parsed but **never written**.

### 9.4 Launch contract (`start-run.ps1` → `quick-train.ps1`)

1. Record demos (Editor batch mode). Build `Builds/Runs/<SceneLeaf>/TrainingEnv.exe`.
2. `mlagents-learn <config> --results-dir=results --env=<TrainingEnv.exe> --num-envs=N --no-graphics
   --run-id=<run> --base-port=<port> --env-args -envParamsOut="results/<run>/envparams.txt"
   -gitSha=<sha>[+dirty]`. Port stride 65; up to 3 retries within 90 s.
3. Ready when the trainer stdout shows a lesson line or a `Step: N … Mean Reward:` line. Fail on
   `FATAL|Failed all \d+ attempts|did not complete` in the train log, or an early exit.
4. Player logs are at `results/<run>/run_logs/Player-<i>.log`. The documented checks grep
   `PHYSICSRATE` and compare `JOINT FREEZE` lines between the demo log and the Player log.

## Open re-survey items

- The ML-Agents worker command line in this fork (`--mlagents-port`, `-logFile`, batch-mode flags).
- The exact root-motion speed of each demo controller.
- Not verifiable without running Unity:
  - whether PhysX reports velocities for `MovePosition`-driven kinematic bodies;
  - PhysX-computed inertia tensors (the exporter reads them back in play mode);
  - whether trigger Eye capsules count toward the Head mass;
  - Unity drive spring units;
  - the exact D6 swing-cone mapping.
- (Resolved: Jolt lambdas are public; contact impulses need patch P-C; see [jolt-readouts.md](jolt-readouts.md).)
