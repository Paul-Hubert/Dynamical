# HumanBot port review

*2026-11-09. Review of the plan and of P0–P3 against the Unity sources `HumanBotAgent.cs`, `HeidelBot.prefab`
and `ImitationRecorder.cs`, plus everything they call. Unity tree: `Dynamical-noToes` @ `815fb4b41`. Engine: `af7bdd7`.*

**Verdict.** The architecture stands and no finished phase has to be redone. The review found 3 errors that would
have produced a wrong or crashing agent, 12 smaller corrections, and one Unity artefact that was not known: every
Unity demo holds huge velocity spikes. All the corrections are now in the plan docs. Decisions only you can make are
in [open-questions.md](open-questions.md).

No code was changed. How it was checked: every line of the three files and of `BotAgent`, `BotJoint`, `JointAgent`,
`BotParameters`, `Referential`; the prefab parsed object by object; `Training.unity`, `DeVitaArenaH173.unity` and the
configs read for overrides; the real `Demonstrations/HeidelBot.demo` decoded with the fork's protobufs.

## Findings

Severity: **A** = wrong behaviour or crash if built as planned; **B** = a test or spec that would fail or mislead;
**C** = a fact to correct.

| # | Sev | Finding | Evidence | Plan change |
|---|---|---|---|---|
| R1 | A | **Energy speed uses the Spine, not the Hips.** UA §4.1 and agent-runtime said Hips. `p.root` is `mixamorig:Spine` | `HeidelBot.prefab` root → fileID `2740932198972905219` → `mixamorig:Spine`; `EnergyRewardProvider.cs:317` | agent-runtime, UA §4.1 |
| R2 | A | **Normalising a zero vector.** The flat walker commands speed 0 on half of its draws (`speeds` {0, 1.36}). Unity's `.normalized` returns zero; `glm::normalize` returns NaN. It follows that the root reward at speed 0 is exactly 0, not "undivided" | `HumanBotAgent.cs:171`, `:179-180`, `:329`; prefab `speeds` | agent-runtime: `safe_normalize` |
| R3 | A | **The reward must see the previous decision's command.** Unity runs `CalculateRewards`, then `UpdateObjective`, then observes (`:305-315`). simulation.md ran `Observation` before `Rewards`, so a shared snapshot would give the new command to the old window | `HumanBotAgent.cs:302-319` | simulation.md: `Rewards` before `Observation` |
| R4 | B | **Every Unity demo has velocity spikes.** See below | decoded demo | Q1; S9.1, UA §6 |
| R5 | B | **The knee fix moved the wrong thing.** Moving "the drive's zero" to 2.1° shifts every knee target and the limits' meaning by 2.1°. Only the spawn pose should bend | agent-runtime "Knee rest policy" against `BotJoint.GetTargetRotation` | Q6 (default written into the docs) |
| R6 | B | **Twist limit sign.** My derivation here (`[−hi, −lo]`) was wrong: Unity's joint angle already runs opposite the numeric rotation about its axis, so the mirror cancels it and the limits are unchanged. Measured by the Unity limit probes to 0.001° | limit probes, T6.1 | conventions, physics, T6.2, T6.3 |
| R7 | B | **S5.2 cannot pass on root joints.** In the clone, the unbodied Hips animates while the root body follows root motion, so root→UpLeg and root→Spine distances change through the gait | `ImitationRecorder.MoveTransform`; prefab hierarchy | S5.2 and rig-animation exclude the 3 root joints and report them |
| R8 | B | **Track length.** Recording runs to the end of the current episode: the real demo has 10 155 decisions for `NumStepsToRecord` 10 000, or 20 310 ticks, more than the 20 000 frames of the example | decoded demo meta | assets.md: `frames ≥ 2·N + episode_ticks` |
| R9 | B | **`TrackActions` must not loop or restart for demos.** Unity's clone walks on across episodes. Restarting gives identical episodes | `ImitationRecorder`; spikes grow linearly with distance | inference-and-replay |
| R10 | B | **T9.2 and S9.1.** `_1` headers are not x = 0: pitch leaks head error into x (measured up to 0.0045). S9.1's channel stats are dominated by the spikes | decoded demo | T9.2 is `_2` only; S9.1 excludes 3 decisions after each reset |
| R11 | B | **Disturbances are off in both reference lines.** `pushing`, `pulling`, `randomInit` are 0 in the prefab, not overridden in either scene, absent from their configs. P8's check "a push visibly hits one segment" would not trigger | prefab; scene overrides; `default-training.yaml`, `devita-h173-*` | Q7; P8 enables a push explicitly |
| R12 | B | **Jolt clamps motor targets to the limits.** `SetTargetOrientationCS` clamps when (ay, az) leaves the cone. Unity keeps the target and pushes against the stop | `SwingTwistConstraint.cpp:186-198` (5.6.0) | physics.md; Q8 |
| R13 | B | **Stats and aggregation.** `Environment/Total Frame Reward` and its two variants were not listed. The bridge sends one averaged value per key (`worlds_env.cpp:117`), so Histogram and per-agent values are missing. `AgentIO::stats` has no aggregation field | `HumanBotAgent.cs:248-255` | agent-runtime; P8 step added |
| R14 | C | `f(s)` "rest forward axis" was ambiguous. It is the segment's own local forward, rotated by its current rotation | `UnorthogonalReferential.Project` | agent-runtime |
| R15 | C | Off-by-one periods: truncation at 2001 ticks, arc every 401, because both compare after incrementing with `>`. The engine uses 2000 and 400 | `BotAgent.cs:179-181`, `HumanBotAgent.cs:102`, `:463` | declared difference (Q9) |
| R16 | C | Arc redraws ignore an env `targetSpeed`: only reset goes through `UpdateParameters` | `HumanBotAgent.cs:470` | agent-runtime |
| R17 | C | Random init moves the whole hierarchy but gives −4 m/s to the root body only | `BotAgent.Randomize` | agent-runtime |
| R18 | C | `maxFrames` is not a Unity env param (conventions said it was). Env `normalizeSpeedDiff` was accepted but `_2` has one fixed header; 4 of the configs set it to 1 | `BotAgent.UpdateParameters`; `HumanBotAgent.cs:606`; configs | conventions; assets: layout-defining |
| R19 | C | "Initial gains 0.5, targets at rest" never applies: every engine episode starts on a decision. Zero action means mid-range, not rest | `BotJoint.GetTargetRotation` | agent-runtime |
| R20 | C | Resolved UNVERIFIED items: root CCD is ContinuousDynamic (`m_CollisionDetection: 2`); both controllers have Foot IK on; state speeds 0.67 and 0.807316. Unity's friction type is serialized as 2, which is not obviously the patch friction UA and jolt-readouts claim | prefab; controllers; `DynamicsManager.asset:33` | UA; exporter dumps it |
| R21 | C | The Jolt doc that owns the solver settings table is not in either repo | search of both repos | Q13 |
| R22 | C | Docs and code diverged while the sphere roller landed: `src/agent/` vs `src/logic/agents/`, `Outbox` vs `AgentIO`, P11's `PolicyRunner` and `ModelWatcher` already built | `src/logic/agents/agent_io.h`, `src/onnx` | phases.md status; `AgentIO` extended rather than replaced |

### R4: demo spikes

`Demonstrations/HeidelBot.demo` (DeVita H173): 10 155 decisions, 21 episodes, 501–502 decisions each, zero actions.
Joint speed: p99 3.20 m/s, maximum 3.37 m/s **outside resets**. At the 20 episode starts:

| Decision after reset | How often | What it holds | Max joint speed |
|---|---|---|---|
| 1st | all 20 | the agent's rest pose, header y = 0, stack zeroed | 1.8–58 m/s |
| 2nd | every other reset, 10 of 20 | the clone's pose | 1 548 m/s at the first, growing to **28 400 m/s** |

The cause: `Restart` pastes the agent back to the spawn pose and records `lastPosition` there; the next `MovePosition`
returns it to the clone, which has kept walking (distance / dt: 25 m per episode × 60 Hz). The spike lasts one tick,
and episodes are 1001 ticks, so it lands on a decision tick only every other episode. Those samples enter the GAIL
discriminator raw: the fork's `RewardSignalSettings` default to `normalize: false` (`settings.py:141`, `:207`), and no
reference config overrides it.

The engine's recorder, as planned, has neither frame. So the new demos are cleaner, but **the Unity reference runs
were trained against spiked demos**, which is a confound in the P12 comparison.

## Finished phases and code

| Phase | Review result |
|---|---|
| P0–P2 | Sound. Reports still say "Human sign-off: pending" (Q15) |
| P3 | Sound: the fork facts hold. The Unity reference runs of the P3 report's open items do not exist yet for the flat walker (Q2) |
| Sphere roller | Useful ahead of schedule: `mode=train`, `AgentIO`, `PolicyRunner`, `ModelWatcher`. Keeps the reward-target snapshot right. RESET stagger stays sphere-only: Unity has none |
| P4 onward | Not started. No Unity exporter, `resources/agents/` or patches P-A / P-C exist |

## Line-by-line accounting

Status: **Same** = reproduced in intent; **Changed** = declared difference; **Off** = unused by the reference lines,
accepted; **Later** = P15.

### `HumanBotAgent.cs`

| Item | Status | Where |
|---|---|---|
| `Initialize`: `heightFactor`, `est0` from the spawn pose | Changed: rest pose (`_1` reads the golden) | agent-runtime Header |
| Feet `collisionPenalty = 0` | Off (global weight 0), `Collision/*` kept | agent-runtime Rewards |
| `speed` cache, `targetSpeedObs`, `dyingObs` | Off | prefab 0 |
| `nogail` sensor, `CollectGoalObservations` | Later: only `DeVitaArenaH173NogailFeet` | P15 |
| `OnEpisodeBegin` order | Same, explicit | agent-runtime Reset |
| `ArcUpdate`, `RandomSpeed`, `SetTarget` | Same; period R15; override R16 | agent-runtime |
| `KillCondition`, `Kill`, `DeathRewards` | Same; lockstep parking | simulation.md |
| `OnEpisodeEnd`, `WinCondition`, `WinRewards` | Same (`winReward` 0) | agent-runtime |
| `CalculateRewards`: head terms | Same, weight 0, only while dying | agent-runtime |
| `CalculateRewards`: root | Same; R2, R3 | agent-runtime |
| `CalculateRewards`: stats, normaliser maxima | Same keys | T8.7 |
| `Terrain.activeTerrain` slope stats | Off: no terrain on either line | — |
| `CollectImitatedObservations` | Changed: orthogonal heading, true COM velocity | D3 |
| `speedDiffZeroRecording`, `speedDiffZero` | Same / Off | `Recording` |
| `UpdateObjective` | Same; snapshot R3, reset R15 | simulation.md |
| `Push`, `Pull`, `GetRandomBody` | Off; Q7 | agent-runtime |
| `GetHeadError`, `EstimateHeight` (`max(…, 0)`) | Same (`clamp01` covers the 0) | agent-runtime |
| `UpdateParameters` names | Same: all in the bridge table | mlagents-bridge.md |

### `BotAgent.cs`, `JointAgent.cs`, `BotJoint.cs`

| Item | Status | Where |
|---|---|---|
| `PhysicsRate.Resolve`, integer scaling | Changed: control rate fixed at 30 Hz; arc also scales | conventions |
| `ApplyRandomSeed` (seed + worker id, one stream) | Changed: per-world streams from `seed` | simulation.md |
| Decision phase (training: global) | Same; inference stagger not reproduced (Q10) | simulation.md |
| `maxFrames = 0` when not training | Q10 | inference-and-replay |
| `AddReward × rewardFactor`, training only | Same | agent-runtime |
| `Heuristic` empty ⇒ zero demo actions | Same (verified 0 in the demo) | inference-and-replay |
| `Randomize`, `RandomRotation` | R17 / Same | agent-runtime |
| Break force, `SetFreezing`, `immobile`, `idleMeasurement`, mannequin, `ArmObsFreezer`, `deactivated` | Off | not on the reference lines |
| `freezeObs` (6 arm joints authored, master switch 0) | Off, layout-defining | assets.md |
| `GetJoints` order | Same; listed explicitly | body file |
| `IgnoreCollisions` | Same: group filter | physics.md |
| Energy channels 0–2, closure, indirect | Changed: exact channel 3 | E7 |
| `Configure`: masses, max angular velocity 20 | Same | physics.md |
| `CalculateObservations`, right-side negation, `jointSpace` | Changed: `_2`; `_1` oracle | agent-runtime |
| `CalculateVelocity`, `lastPosition` | Same; spike fixed | `AnchorHistory` |
| `OnActionReceived`: gains, targets, ω | Changed: swing · twist, no Euler | agent-runtime |
| `TargetAngularVelocityFromAction` | Same: `ω = e · max` | agent-runtime |
| `SetDrive`, factors, Head spring 0.5 | Same | prefab, verified |
| `angularVelocityFactor` | Off (0): accepted, warned if non-zero | mlagents-bridge.md |
| Collision callbacks, `ApplyDamage`, `DamageStrength` | Off, stats kept | agent-runtime |

### `HeidelBot.prefab`

| Block | Status |
|---|---|
| `BehaviorParameters`: 138 × 2, 120 actions | Same, contract |
| `HumanBotParameters` | Same: exported resolved |
| 16 Rigidbodies, 15 ConfigurableJoints, colliders, material | Same: exported; all checked against UA §1 |
| `EnergyRewardProvider` | Same on channel 3. Prefab values differ from code defaults (`minSpeedFloor` 0.1, `speedEdgeWidth` 0.15): export resolved values |
| `LegacyRewardProvider` | Off: `legacyRewards` 0 returns before any stat |
| `DemonstrationRecorder`, `Animator`, `ImitationRecorder` | Replaced by the bake and `mode=record-demo` |
| `AgentEvents`, `ColliderRenderer`, `Size`, `Length`, `ColliderConfig`, `Speed`, meshes | Not ported: plumbing, view only, or disabled |

### `ImitationRecorder.cs`

| Item | Status |
|---|---|
| Forces `training`, `maxFrames` 1000, `dieOnFall` false, `torqueFactor` 0 | Same in `walker_demo` |
| Clone with root motion, kinematic copy by name | Replaced by the bake; world poses of the 16 bodies |
| `MoveTransform`, one-tick lag | Changed: `MoveKinematic`, documented |
| Random z offset at `SetRecording` | Dropped: the next `MovePosition` overwrites it |
| `NonCollide` layer, projection off | Same: kinematic, non-colliding |
| `PickAnimation` | Bake covers each controller (both flat entries are the same controller) |
| `CopyAction`, `AssertReversible*` | Dropped: reach nothing observed |
| `timeScale` 20 | Not needed |
| Spikes and rest-pose frames (R4) | Q1 |

## Declared differences added by this review

Each is now in the docs; none needs code to be undone.

1. Demo spikes and rest-pose frames do not exist in engine demos (Q1).
2. Periods are 2000 and 400 ticks, not 2001 and 401 (Q9).
3. Motor targets are clamped to the limit surface (Q8).
4. No pre-decision ticks with stale gains and targets.
5. Demos end at a whole episode: the requested count is a minimum, as in Unity.
