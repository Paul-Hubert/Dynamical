# Open questions for P4 onward

*2026-11-09. From [humanbot-port-review.md](humanbot-port-review.md). Answered 2026-11-09: **all defaults**, except
Q2 (one Unity run, 1.36 m/s only, 40M steps, the sole reference) and Q4c (Python launchers in `tools/train_walker.py`
instead of editing `start-run.ps1`).*

"Blocks" is the first phase that cannot finish without an answer.

## Reference and comparison

**Q1. Demo spikes (blocks P9).** Every Unity demo holds a rest-pose frame at each episode start and, every other
episode, one frame with joint speeds from 1.5 to 28 km/s. They reach the discriminator unnormalised (review R4). The
engine's demos won't have them.
- **Default: drop them**, and record it as a known confound of the Unity baseline.
- Alternative: add a test-only `demo_compat_spikes` flag, used only if P12 fails its gate.

**Q2. Which runs are the Unity references (blocks P12, P13).** `results/` holds only H173 runs (`dhntec200`, `dhntmc200`,
`dhntroot200`, `dhgrfec`, `dhgrfmc`, and the lean runs). There is **no noToes flat-walker run**.
- a. Flat walker: **default: train one now in Unity** (`default-training.yaml`, 40M steps; about 26 h at the measured
  424 steps/s), or name a run elsewhere.
- b. How many seeds? The gate says "within the reference's seed spread", which needs at least 2. **Default: 3 seeds**
  (78 h of Unity). With one seed, the gate becomes a fixed ±15 % band.
- c. DeVita H173: which run? **Default: `dhntec200-DeVitaArenaH173-ysbb`** (equal weights, no GRF).

**Q3. Unity commit to export from (blocks P4).** The tree has uncommitted changes to `EnergyRewardProvider.cs`,
`JointAgent.cs`, `RealizedWork.cs`, and new configs. None touch the prefab or the scenes.
- **Default: you commit them, or tell me to; export from the resulting commit.**

**Q4. What can run in Unity (blocks P4).** The exporter, limit sweep, drive steps, rollout logger and track bake are
new Editor code in the Unity repo.
- a. May I add `Assets/Editor/JoltExport*.cs` to `Dynamical-noToes`? **Default: yes, on a new branch.**
- b. May I run Unity in batch mode myself (`Scripts/Get-UnityExe.ps1`), or will you run the commands? **Default: I run
  them, when the Editor is closed.**
- c. May I change `start-run.ps1` and `quick-train.ps1` for `-Engine dynamical` (P10)? **Default: yes, Unity path
  unchanged.**

**Q5. Where do generated data live (blocks P4).** Body files and params are small. Pose tracks are about 20 000 × 16 × 7
floats (9 MB each, 3 controllers), golden rollouts about 45 MB of JSON, demos about 17 MB.
- **Default: body, params, and a shortened golden set in git; full tracks, rollouts and demos in Git LFS.** The tests
  that need the full set skip with a message when it is missing.

## Body and drives

**Q6. Knee rest (blocks P4).** The plan moved the "drive zero" to 2.1°, which also shifts knee targets by 2.1°.
- **Default: only the spawn pose bends to 2.1°**, so the knee starts inside its 2° stop. Constraint frames, limits and
  targets keep Unity's straight reference.

**Q7. Disturbances (blocks P8).** Push, pull and random init are off in both reference lines; 52 other configs mention
them. Unity's push is up to 350 N·s on any one body: up to 770 m/s on a hand, above Jolt's 500 m/s limit, so it can
hit clamps.
- **Default: build push in P8 (off in the params, used by a test); pull and random init go to P15** (random init needs
  `maxDyingFrames` ≥ 60 to mean anything).
- Alternative: skip all three until P15.

**Q8. Targets outside the swing cone (blocks P6).** With ay and az both large, Unity pushes against the limit; Jolt
clamps the target to the cone, so the motor no longer fights the stop.
- **Default: accept the clamp**, and count how often it happens as a stat. It is cleaner for the energy books.

**Q11. Angular damping for the flat walker (blocks P12).** The plan uses 0 everywhere for clean energy books. Unity
uses 0.05, and the flat walker doesn't use the books.
- **Default: 0 everywhere**, one body file, no extra variable. Revisit only if P12 misses its gate.

## Task

**Q9. Off-by-one periods (blocks P8).** Unity truncates at 2001 ticks and redraws the arc at 401. The engine's windows
need whole numbers.
- **Default: 2000 and 400.**

**Q10. Viewer and eval behaviour (blocks P11).** Unity never truncates outside training (`maxFrames = 0`) but still
resets on a fall, and it staggers decisions across agents.
- **Default: `walker_view` matches it**: no truncation, resets on a fall, no stagger (one agent per world).
- **Default: `mode=eval` keeps 2000 ticks** so summary stats compare with training.

**Q12. Reference speeds (blocks P12).** The flat walker commands 0 or 1.36 m/s with equal probability, and its demo
only walks. Nothing to port differently, but the gate's `root speed vs target` needs a choice.
- **Default: judge the gate at 1.36 m/s only, and report 0 m/s (dead rate, drift) separately.**

## Project

**Q13. The Jolt doc (blocks P6).** "Joint Kinetics & Actuator Energy in Jolt Physics" (2026-10-29) owns the solver
settings and is referenced by physics.md, but it isn't in either repo. Where is it?
- **Default: you add it as `docs/physical-agents/jolt-kinetics.md`.** Without it I would tune the solver settings from
  scratch in P6.

**Q14. Friction type (no block).** `DynamicsManager.asset` serializes `m_FrictionType: 2` and `m_ImprovedPatchFriction: 1`.
The docs say "patch friction". Which does your Project Settings panel show? The exporter will dump the asset either
way, so this only corrects the record.

**Q15. Sign-off of P0–P3 (no block).** All four reports say "Human sign-off: pending". **Default: you review them
when convenient**; P4 proceeds regardless.

**Q16. How to run P4 onward (blocks start).**
- a. **Default: autonomous, as P0–P3**, with a new prompt and a stop after each phase that needs a Unity comparison
  you must judge (P4, P5, P6, P12).
- b. **Default: branch `agents/p4-p11` from `master`** (`af7bdd7`, equal to this branch). Say if you want it pushed.

**Q17. Training budget (blocks P12).** Three seeds of 40M steps. With 16 processes, the engine is expected near the
trainer's ceiling (4 300 steps/s without GAIL, so about 2.5 h per seed).
- **Default: run them overnight, one at a time, on request, with wandb offline as the sphere runs are.**

**Q18. The one wandb run (no block).** `dynamical/Dynamical/null_smoke` was uploaded in P3 before the offline guard.
**Default: leave it**; delete it yourself if you don't want it there.
