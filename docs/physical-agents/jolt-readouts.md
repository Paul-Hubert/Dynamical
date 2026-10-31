# Jolt readouts: constraint forces, contact impulses, friction

> **Plan of record: Jolt 5.6.0** (overlay port, no vcpkg baseline bump), `EMotorState::PositionAndVelocity`,
> patches **P-A** (limit axes) and **P-C** (post-solve contact impulses). §1–§4 document the 5.5 source as
> verified; §0 lists what 5.6 changes.

*Verified 2026-10-31 against the Jolt source vcpkg builds:
`$VCPKG_ROOT/buildtrees/joltphysics/src/v5.5.0-ca68e48c31.clean/Jolt/`. Paths below are relative to
`Jolt/Physics/`.*

## Summary

| Question | Stock Jolt 5.5 (verified) | With a small patch |
|---|---|---|
| Joint constraint impulses (point, limits, motors) per collision sub-step | **Yes.** Public `GetTotalLambda*()`, read from a `PhysicsStepListener` or after each `Update` | — |
| World axes of the limit impulses | No, the axes are private | Accessors (3 lines) |
| Contact impulses (normal + 2 friction axes) after the solve | **Computed, but not exposed.** Stored in private caches | A post-solve callback or accessor (~30 lines) |
| Friction recoverable without the patch | **Yes, per body, as a total.** Foot momentum balance minus joint impulses gives the full contact impulse; on flat ground its horizontal part is friction | Per contact point with the patch |
| Unity-like SLERP damping toward a target angular velocity | **No.** Position motors ignore the target velocity | One argument (2 lines) |

## 0. Upstream status: Jolt master and v5.6.0

*Checked 2026-10-31 against `jrouwe/JoltPhysics` master @ `bb3e363` (2026-10-30, version 5.6.1
unreleased) and the release notes for **v5.6.0** (tag `e77f175`). vcpkg master ships `joltphysics`
**5.6.0#1**; this repo's baseline still pins 5.5.0.*

| Need | Upstream | Consequence |
|---|---|---|
| Target angular velocity in position motors (P-B) | **In v5.6.0:** `EMotorState::PositionAndVelocity` gives `F = k·(target − current) + c·(ω_target − ω)` (`MotorSettings.h`). `SwingTwistConstraint` passes `-mTargetAngularVelocity[i]` as the bias (`SwingTwistConstraint.cpp:298`, `:340`). It is enabled when the spring has stiffness **or** damping, so a zero spring with non-zero damping still damps toward the target, as Unity does when the spring action is −1. Same soft-constraint math as derived in §2 | **P-B is not needed on 5.6.0.** Use `PositionAndVelocity` |
| Limit-axis accessors (P-A) | Not present. The axes are still private members of `SwingTwistConstraintPart` | P-A is still needed |
| Post-solve contact impulses (P-C) | Not present. `ContactListener` still has only Validate / Added / Persisted / Removed. `PhysicsSystem` exposes no contact manager. The lambdas still go only into private caches via `sStoreAppliedImpulses` | P-C is still needed |
| `EstimateCollisionResponse` | A public helper that **estimates** contact impulses from a manifold, called in `OnContactAdded`, before the solve. In 5.6 it returns 2 linear + 1 angular friction impulse | Not a post-solve readout; unusable for exact books |
| Upstream friction model | **Changed in v5.6.0.** Friction is no longer per contact point. Per manifold there are 2 linear constraints plus 1 angular (torsional) constraint at the **average contact point**, bounded by μ·Σλ_n and μ·Σ(r_i·λ_n,i). Cached as `mFrictionLambda[2]` + `mAngularFrictionLambda` per manifold (`ContactConstraintManager.h:306-307`) | The P-C payload becomes: per point `λ_n` and position; per manifold the two friction lambdas, the angular friction lambda, the average point and the tangents. The momentum balance (§4) is unaffected. The model resembles PhysX's patch friction (Unity used improved patch friction), which is good for feet |

**Other 5.6.0 changes that matter:**
- 5.6.0 adds GPU compute interfaces (`JPH_USE_DX12`, `JPH_USE_VK`, `JPH_USE_MTL`, `JPH_USE_CPU_COMPUTE`)
  and GPU hair. Build the port with these **off**; the engine owns its own Vulkan device.
- The friction-model change shifts the simulation slightly (APIChanges, 20260531).
- **Master only (unreleased):** `PhysicsSettings::mDeterministicSimulation` is removed (the simulation
  is always deterministic), and the warm-start impulse ratio is clamped for uneven time steps.

**Recommendation.**
- **Move to 5.6.0 in S2 (phases P2)** through an overlay port, with **no vcpkg baseline bump**. The
  remaining patches are added to that overlay in M2.
- Use `PositionAndVelocity` and drop P-B.
- Write P-A and P-C against 5.6.0's per-manifold friction layout.
- Propose P-C upstream as a post-solve contact callback. The same request covers P-A.
- Master's base scene only uses `RigidBody` boxes and spheres plus terrain mesh colliders, so the bump
  should be source-compatible. Re-run S1's `base` check after it.

## 1. Joint constraint impulses per sub-step

**Lambda semantics.**
- Every constraint part keeps `mTotalLambda`: the total impulse it applied during the last velocity
  solve, including the warm-start impulse.
- `AngleConstraintPart::WarmStart` (`Constraints/ConstraintPart/AngleConstraintPart.h:164`) rescales the
  previous value; the velocity iterations then add to it.
- Position iterations do not touch it.
- A part that is not active is `Deactivate()`d, which sets λ = 0 (`:148-151`).

**Public accessors** (`Constraints/SwingTwistConstraint.h:149-153`):

```cpp
Vec3  GetTotalLambdaPosition();   // point constraint, world-space linear impulse (+ on body 2, − on body 1)
float GetTotalLambdaTwist();      // twist limit
float GetTotalLambdaSwingY();     // swing limits
float GetTotalLambdaSwingZ();
Vec3  GetTotalLambdaMotor();      // motors, about the constraint axes of body 2: (twist, swing Y, swing Z)
```

The point-constraint sign follows `PointConstraintPart.h:57-63`: body 1 −, body 2 +.

**Reading per collision sub-step.** `PhysicsSystem::Update(dt, inCollisionSteps, …)` splits `dt` into
equal steps (`PhysicsSystem.cpp:213`). Two ways to read each one:

- **A. Step listener.** `PhysicsStepListener::OnStep` runs at the **start** of every collision step,
  before gravity and constraint setup (`PhysicsSystem.cpp:388-397`, `JobStepListeners` `:680`).
  - At step k, the lambdas still hold step k−1's impulses, and the body velocities are step k−1's end
    velocities.
  - The last step is read after `Update` returns.
  - Listeners are skipped when no body is active (`PhysicsStepListener.h:33`). Agent bodies never
    sleep, so this does not apply.
- **B. Our own sub-stepping (recommended).** Call `Update(h_sub, 1, …)` n times per physics tick and
  read after each call.
  - It is the same work, since every Jolt collision step already runs full collision detection.
  - The control flow is simpler, and nothing runs inside Jolt's job graph.

**Work per sub-step.**
- `W = λ · ω_rel,mid` for angular parts, and `λ · v_rel,mid` at the anchor for the point part, where
  "mid" averages the start-of-step and end-of-step relative velocities.
- Gravity's Δv is inside that average. The error is second order in h and is visible in the closure
  residual.

**Axes.**
- **Motor axes** are the columns of `R2 · ConstraintToBody2`, evaluated at the start of the step
  (`SwingTwistConstraint.cpp:216-220`). They can be rebuilt from public data:
  `GetConstraintToBody2()` and body 2's rotation, read in the step listener or before each sub-step.
- **Limit axes** (`mWorldSpaceTwistLimitRotationAxis`, `…SwingLimitY…`, `…SwingLimitZ…`) are
  private run-time members of `SwingTwistConstraintPart` (`ConstraintPart/SwingTwistConstraintPart.h:587-589`).
  The Z axis is flipped when the min limit is hit (`:367`). **Patch P-A** adds const accessors.
  Without it, limit work can only be found as a residual.

## 2. Drive semantics: position motors ignore the target angular velocity

> 5.5 history. On 5.6.0 use `EMotorState::PositionAndVelocity` (§0); patch P-B is not needed.

`SwingTwistConstraint::SetupVelocityConstraint` (`Constraints/SwingTwistConstraint.cpp:205-320`)
handles the motor states as follows:

| Motor state | Constraint | Target angular velocity |
|---|---|---|
| `Velocity` | Rigid velocity constraint with bias `−mTargetAngularVelocity[i]` (`:276`, `:312`) | Used. No spring |
| `Position` | Soft constraint `CalculateConstraintPropertiesWithSettings(dt, …, axis, 0.0f, rotation_error[i], spring)` (`:284`, `:318`) | **Not used**: the bias is hard-coded to 0 |

**What the position motor applies.** The soft constraint (Catto, `ConstraintPart/SpringPart.h:16-60`)
is `J·v + softness·λ + b = 0`, with `softness = 1/(h(c + hk))` and `b = inBias + h·k·softness·C`.
Solving for the force gives

```
F = λ/h = −k·C − (c + h·k)·(J·v + inBias)
```

So with `inBias = 0` the damper pulls the relative angular velocity toward **zero**. Unity's SLERP drive
damps toward `targetAngularVelocity` (UA §2.4), and that is the ω half of the 120 actions, which
experiments use (`maxTargetAngularVelocity` flips the sign of net joint work).

**Patch P-B** passes `-mTargetAngularVelocity[i]` instead of `0.0f` in the two `Position` calls. The
force becomes `−k·C − (c + h·k)(ω_rel − ω_target)`: a spring–damper toward a moving target, as intended.
`SetTargetAngularVelocityCS` already exists and is merely ignored in Position mode.

**The stock alternative is not exact.** Applying `+c·ω_target` as an explicit external torque pair
(child +, parent −) next to an unpatched position motor gets close, but it mixes an explicit term with
the implicit `(c + h·k)` damping. It also has to be booked separately in the energy recorder.

## 3. Contact impulses after the solve

**Where Jolt keeps them.**
- `ContactConstraintManager::StoreAppliedImpulses` (`Constraints/ContactConstraintManager.cpp:1705-1719`)
  runs after the velocity solve of **every collision step** (`PhysicsSystem.cpp:1449`, `:1549`).
- It copies each contact point's `mNonPenetrationLambda` and `mFrictionLambda[2]` into the
  `CachedContactPoint` used to warm-start the next step.

**None of it is public:**
- `ContactConstraint` and `WorldContactPoint` (normal, tangents, the three `AxisConstraintPart`s) and
  the `CachedManifold` / `CachedContactPoint` caches are all private (`ContactConstraintManager.h:259-300`,
  `:400-500`).
- `PhysicsSystem` has no accessor for `mContactManager`.
- The constraint buffer is freed at the end of `Update` (`FinishConstraintBuffer`, `PhysicsSystem.cpp:647`)
  and recycled between steps (`:468`).
- `ContactListener::OnContactAdded` / `OnContactPersisted` run during collision detection, **before**
  the solve, so they see geometry and settings but no impulses (`Collision/ContactListener.h:110-123`).
- The friction axes come from `ContactConstraint::GetTangents` (`ContactConstraintManager.h:459`), which
  builds an orthonormal pair from the normal.

**Stock workaround: serialised state.** `PhysicsSystem::SaveState(stream, EStateRecorderState::Contacts)`
serialises the cached manifolds, lambdas included. It is public but uses an internal binary layout,
holds only the last step, and is brittle. It is fine for a one-off check, not for the recorder.

**Patch P-C, the recommended route** (~30 lines on 5.6.0):
- an optional `ContactImpulseListener` on `PhysicsSystem`, called from `sStoreAppliedImpulses` after
  every collision step's velocity solve;
- **payload per manifold (5.6 friction model):**
  - the two `BodyID`s and sub-shape IDs;
  - the world normal;
  - per point, the world position and `λ_n`;
  - per manifold, the average friction point, tangents `t1`, `t2`, `mFrictionLambda[2]` and
    `mAngularFrictionLambda` (torsional, about the normal).
- **The contact wrench on body 2** = `Σ λ_n,i·n` applied at the points, plus `λ_t1·t1 + λ_t2·t2` at the
  average point, plus a torsional impulse `λ_ang·n`. The opposite acts on body 1 (*verify* the sign in
  `sWarmStartConstraint`).
- It fires from parallel solver jobs, so the listener must be thread-safe. Training worlds use
  `JobSystemSingleThreaded`.

## 4. Friction without patching: momentum balance

For each foot body over a sub-step of length h:

```
J_contact,lin = m·(v_end − v_start) − m·g·h − Σ_joints J_point(sign by body side)
J_contact,ang = I·ω_end − I·ω_start (about the COM, world inertia at mid-step) − Σ_joints (angular parts + r × J_point)
```

- **On flat ground** with one contact surface, the normal is +y. So `J_contact,lin.y` is the normal
  impulse, and the horizontal part is **friction**.
- **Centre of pressure:** the point on the ground plane where the contact torque has no horizontal
  component.
- **This is exact, provided nothing else changes the foot's velocity during the step.** Jolt changes
  velocities outside constraint lambdas in four places:

  | Source | Location | Handling |
  |---|---|---|
  | Velocity clamping after the solve | `ClampLinearVelocity` / `ClampAngularVelocity`, `PhysicsSystem.cpp:1645-1646` | Keep `maxAngularVelocity` away from saturation, or count clamp events and flag those ticks |
  | Linear-cast CCD | `PhysicsSystem.cpp:2245`, `:2301` | Use `Discrete` motion quality for agent bodies (walking speeds against a thick floor box do not need CCD), or flag CCD ticks |
  | Damping | `MotionProperties::ApplyForceTorqueAndDragInternal` | The body version sets damping to 0 |
  | Gravity | integration | Subtracted analytically above |

- Unity's `energyFullGrf` did the same balance on PhysX (UA §4). On Jolt it is exact, and the joint
  impulses are read directly instead of being reconstructed by Newton–Euler.

**When the patch is still needed:** per-point forces (for example per-foot-box pressure distribution),
non-flat ground with several contact normals per foot, and foot–foot or foot–body contacts. Without
P-C, the momentum balance books those as "contact" too.

## 5. Recommended setup

1. **Jolt 5.6.0 through a vcpkg overlay port** (`ports/joltphysics`). There is **no baseline bump**:
   that would also move EnTT, SDL2, OpenXR and others and put the `base` gate at risk. The overlay:
   - builds with the GPU compute options off, and optionally cross-platform determinism;
   - carries P-A (limit-axis accessors) and P-C (contact impulse listener) as patch files, each
     upstreamable.
2. **Motors:** `EMotorState::PositionAndVelocity` with stiffness and damping per joint
   (physics.md "Drives").
3. **Recorder:**
   - our own sub-stepping (`Update(h_sub, 1)`);
   - joint lambdas and P-C impulses read inside `PhysicsStep` after each sub-step;
   - the momentum balance as a cross-check;
   - the closure residual per tick.
4. **Agent bodies:** `Discrete` motion quality, damping 0, `maxAngularVelocity` with headroom, and a
   clamp-event counter as a stat.
5. **Tests** (`[energy]`, phases P6–P7):
   - the P-C impulses equal the momentum balance on a box and on single-foot stance (flat ground) to
     1e-4;
   - the `PositionAndVelocity` motor tracks a constant ω target with ≤ 2 % steady-state error;
   - the closure residual stays under 2 % with P-A limit work included.

**Upstreaming.** Propose P-C as a post-solve contact callback and P-A as accessors. Drop either patch if
upstream adds an equivalent.
