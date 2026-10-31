# Physics

**A first-class Jolt implementation.** Reproduce what the experiments depend on: the body (masses,
shapes, limits), the drive intent, friction, and an exact energy readout. Tune everything else fresh
for Jolt. The Unity reference is [unity-analysis.md](unity-analysis.md) ("UA"); the Jolt doc owns the
solver settings and the energy recorder.

## Changes to master's Jolt code

Master has `PhysicsWorld` (`src/physics/physics_world.*`), `PhysicsSys` (`physics_sys.*`) and
`TerrainColliders`.

| # | Change | Why |
|---|---|---|
| P1 | **`JoltRuntime`** RAII guard in `main`, `train_main` and the test runner. It owns `RegisterDefaultAllocator`, `Trace`, `AssertFailed`, `Factory::sInstance` and `RegisterTypes`/`UnregisterTypes`, which today run in `PhysicsWorld`'s constructor and destructor | With two worlds, the first destructor unregisters types the second still uses |
| P2 | **`PhysicsConfig`**: `threads` (1 → `JobSystemSingleThreaded`, else `JobSystemThreadPool`), `max_bodies`, `max_body_pairs`, `max_contacts`, `temp_bytes`, `gravity`, `PhysicsSettings` overrides (velocity and position steps, speculative distance, penetration slop, restitution threshold), `substeps` (our own sub-stepping; Jolt's `inCollisionSteps` is always 1) | Training worlds are small (≈ 20 bodies) and single-threaded. `base` keeps 65536 bodies, the thread pool and a 16 MB temp allocator |
| P3 | `PhysicsWorld` lives in the **registry ctx**; its job system member becomes `std::unique_ptr<JPH::JobSystem>` (master: `JobSystemThreadPool`, `physics_world.h:68`) | Builder, drive, contact and energy systems need it; `PhysicsSys` stops owning it |
| P4 | **Layers** become `STATIC`, `MOVING`, `AGENT`, `SENSOR` (broad-phase `NON_MOVING`, `MOVING`) | Agents collide with arena geometry only (UA §5: no self-collision, no agent–agent). Self-collision is handled per ragdoll by a group filter. With one agent per world, `AGENT`-vs-`AGENT` filtering only matters for M9 multi-agent worlds |
| P5 | **Split `PhysicsSys`** into `RigidBodySys` (create and destroy `RigidBody` bodies), `TerrainCollidersSys`, and `PhysicsSys` itself (accumulator, interpolation; used by `base`). Agent worlds use `PhysicsStep` (one step per fixed tick) | `base` keeps its behaviour; worlds get the fixed lane |
| P6 | `RigidBody::Shape::Capsule`; `Ground` component | Flat ground and debug content in worlds |
| P7 | A contact listener that records into a per-world queue | `Contacts` system; the base scene does not install it |

## Ground

**Flat only until M6 (plan E4).**
- `Ground { type = box, size, top_y = 0, material }`: a static `BoxShape` on `STATIC`, top face at
  y = 0, with friction 1. It matches Unity's FlatFloor and the DeVita 150 × 10 × 150 ArenaFloor.
- `OptimizeBroadPhase()` runs once after the ground is added.

**Later.**
- M7, `SlopeGround` (UA §3.6): rotates the static box about the medio-lateral axis through its origin,
  drops it by `footReach·tan|θ| + liftMargin`, and optionally compensates the target speed by cos θ.
  The env param names are kept.
- The lean line trains on a fixed 10° slope from the start, so it waits for M7.
- M9, heightfields.

## Ragdoll build

`RagdollBuilder::build(reg, world, const BodyDef&, Motion motion)` returns the agent root entity.
`motion` is `Dynamic` for training and viewing, `Kinematic` for `rig_anim` and the demo recorder.
The `BodyDef` is already in the engine frame: the Unity importer converted it
([conventions.md](conventions.md)).

1. **Segments.** 16 segments: the pelvis root (origin at floor level) plus 15 jointed segments (UA §1).
   - Compound segments use a `StaticCompoundShape`: pelvis = Hips capsule; Spine = Spine + Spine1
     capsules; Spine2 = Spine2 + both shoulders + neck.
   - Capsules: Unity height includes both caps, along X or Y. Jolt uses the half-height of the cylinder
     along local Y, so convert and rotate.
   - Boxes on feet and hands. The importer bakes the foot scale (1.0517) into the box half-extents.
   - Asymmetric arm shapes stay as exported. Trigger shapes (eyes) are dropped.
2. **Mass properties are provided, never computed** (`MassAndInertiaProvided`):
   - mass = `totalMass · bodyPercentage / 100` (Plagenhoef, sum 99.98 %), with `totalMass` from the
     params file (70 or 69);
   - centre of mass and inertia from the body file, by its declared `inertia_source`:
     - `physx_readback` (v1): what PhysX computed, read back in Unity play mode;
     - `anthropometric`: segment tables (a later body version).

     The importer wraps shapes in an `OffsetCenterOfMassShape` so Jolt's centre of mass equals the
     exported one.
3. **Body settings:**
   - linear damping 0;
   - angular damping from the body version: **0 by default**, for clean energy books (Unity used 0.05;
     UA §5);
   - `maxAngularVelocity` from the **params file** (20, or 20.9044 for H173), env-overridable per episode,
     applied with `MotionProperties::SetMaxAngularVelocity` at reset (Jolt's default is ~47);
   - sleeping off; `Discrete` motion quality (CCD changes velocities outside constraint impulses and
     would break the energy and contact books; walking speeds on a thick floor box do not need it;
     [jolt-readouts.md](jolt-readouts.md#4-friction-without-patching-momentum-balance));
   - layer `AGENT`; collision group `{ragdoll filter, agent id, segment index}`.
4. **Constraints** (next section). Pivot = child bone origin.
5. **No self-collision:** a `GroupFilterTable` disabling every pair.
6. `RagdollSettings::Stabilize()` is **not** called: it changes masses.
7. **Entities:**
   - root: `ArticulatedBody` + `Agent`;
   - per segment: `PhysicsBody`, `Segment`, `SegmentState`;
   - per joint: `JointRef`, `JointState`.

**Reset without rebuilding:**
- set every body's pose to the rest pose (or a track frame) and zero velocities;
- `ResetWarmStart()`;
- clear contacts, energy and finite-difference history.

Rebuild only when the body version changes. A per-episode `totalMass` env param re-applies mass
properties with `MotionProperties::SetMassProperties`, scaling inertia by the mass ratio.

## Joints

One `SwingTwistConstraint` per joint, in `LocalToBodyCOM` space, pivot at the child bone origin.

| Unity (UA §1.2) | Jolt |
|---|---|
| `axis` | twist axis. Forearms in Unity: L (0, 0, 1), R (0, 0, −1); after import (z mirrored): L (0, 0, −1), R (0, 0, 1) |
| `secondaryAxis` | plane axis; normal = twist × plane |
| low/high X limit | twist min/max. Reflection at import can swap the sign; the `[limits]` tests decide |
| Y limit (about the secondary axis) | plane half-cone |
| Z limit (about axis × secondary) | normal half-cone |
| elliptical cone | `ESwingType::Cone` |
| hard stops, no spring or bounce | Jolt's default hard limits |

- **Wide swing limits.** The arm swing limits (174.8° / 112.7°) and the head Y limit (90°) are wide.
  Jolt's cone half-angles must stay below π. The importer clamps at 179° and logs it; `rig_anim`
  checks whether any pose reaches that.
- **The knee rest pose** is moved to 2.1° (the 2° stop plus 0.1°) at import
  ([agent-runtime.md](agent-runtime.md#actions-8--15)).

### Drives

- **Motors.** Swing and twist motors in `EMotorState::PositionAndVelocity`, sharing one stiffness and damping per
  joint. This is the closest Jolt match to Unity's isotropic SLERP spring–damper.
- **Torque limit** from `maxTorque` (200000 means effectively unlimited).
- **Targets** per tick from `HeldDrive`: `SetTargetOrientationCS(q)` with q = swing · twist relative to
  rest, and the motor target angular velocity from the ω actions. Motor state
  **`EMotorState::PositionAndVelocity`** (Jolt 5.6.0): the damper acts on `ω_rel − ω_target`, as Unity's
  SLERP drive does. Plain `Position` ignores the target velocity; on 5.5 this needed patch P-B ([jolt-readouts.md](jolt-readouts.md#2-drive-semantics-position-motors-ignore-the-target-angular-velocity)).
- **Force mode.** Torque is not scaled by inertia, as in Unity's force-mode drive.
- **Gain scale.** Jolt motor stiffness and damping are not Unity spring units, so M2 re-derives a
  scale factor (`drive_gain_scale` in the params) from step-response tests. The default mean gains
  (0.5 · 2983.68 / 0.5 · 336) are the starting point.
- **Scaled bodies** take `rotationSpring` / `rotationDamper` from their params file (H173: 2731.0933 /
  294.2495).

## Contacts and friction

- **Combine rules.** Friction combine = **max**, restitution combine = **min**, via
  `PhysicsSystem::SetCombineFriction` / `SetCombineRestitution`. Agent and floor materials have
  friction 1 and restitution 0, so μ = 1 everywhere (UA §1.2).
- **`Contacts` system.** Per segment it records ground contact on/off, the contact point and the normal
  and friction impulse. Per foot it also records the **full contact wrench**:
  - per contact point from patch P-C (Jolt computes normal and friction impulses per collision step
    but keeps them private);
  - and, without the patch or as a cross-check, from the foot's momentum balance. That is exact for
    the total, and on flat ground its horizontal part is the friction
    ([jolt-readouts.md](jolt-readouts.md#3-contact-impulses-after-the-solve)).
- **Stance** for DeVita uses the normal force threshold `0.05·m·g`.
- **Foot penetration** is logged as a stat.

## Energy

`Energy` implements the Jolt doc's recorder. Unity's channels 0–2, Full GRF and the charge residual
exist to patch PhysX readouts (UA §4); on Jolt, one exact channel replaces them.

**Per collision sub-step, per joint**, from the constraint lambdas (λ / h = generalised force):
- motor work `τ_motor · ω_rel,mid`, split by sign **per joint per sub-step**;
- limit work, separately (≈ 0 for an ideal stop);
- anchor-slip work from the point-constraint lambda (≈ 0, a check);
- the vectors `τ_motor`, `τ_limit` and `ω_rel`, summed per tick so DeVita can project them
  sagittally.

**Per tick:**
- per-foot contact wrench and point;
- whole body E = KE + PE, dE, and the closure residual
  `dE − (Σ motor + Σ limit + contact + gravity work)`.

**Wiring:**
- A new `energySource` value **3 = exact Jolt** is the default. Values 0, 1 and 2 are accepted and
  mapped to 3 with a one-time warning, so old configs load and the substitution is visible.
- The `EnergyProvider/*` reward and stats follow UA §4.1 on channel 3.
- **Constants calibrated on PhysX channels and g = 10** (`energyScale`, the Margaria multipliers)
  **are re-derived**, not carried over.

M2 checks (Jolt doc §8):
1. contact work is 0 in flight phase;
2. ground reaction force from momentum;
3. closure residual under 1–2 %.

**Verified for Jolt 5.5 and upstream 5.6** ([jolt-readouts.md](jolt-readouts.md)):
- motor, limit and point lambdas are public and hold the impulse of the last collision step;
- the recorder sub-steps itself (`Update(h_sub, 1)`) and reads them after each call;
- motor axes are rebuilt from public data;
- limit axes need patch P-A.

## Physics settings

| Setting | Value | Class (UA §5) |
|---|---|---|
| Gravity | (0, −9.81, 0) | D6 (Unity: −10) |
| Physics rate | 60 Hz; 120, 240 or 480 allowed | Intent: 30 Hz control, 33.3 s episodes |
| Solver steps, speculative distance, penetration slop, restitution threshold | Jolt doc settings table | Tuned fresh (Unity: TGS 20/20, adaptive force, offset 0.04) |
| Friction | μ 1, max combine | Reproduced |
| Max angular velocity | 20 rad/s (params file; H173 20.9044), env-overridable | Reproduced explicitly |
| Angular damping | 0 (body-version field) | Intent; 0 for energy closure |
| CCD | `Discrete` for agents | Changed on purpose (exact books); revisit if feet tunnel |
| Sleeping | off | Irrelevant |
| Self-collision | off | Reproduced |

## Jolt build

- vcpkg `joltphysics` 5.5 is in use today. The consumer's compile definitions must match the port
  (`JPH_*` flags propagated by `Jolt::Jolt`).
- **Jolt 5.6.0** through a vcpkg overlay port (no baseline bump), (`ports/joltphysics`) carrying patches
  P-A (limit axes) and P-C (post-solve contact impulse listener, written for 5.6's per-manifold friction).
  GPU compute options off
  ([jolt-readouts.md](jolt-readouts.md#5-recommended-setup)). The same overlay can set
  cross-platform determinism (`CROSS_PLATFORM_DETERMINISTIC`), which the stock port may not expose. It is optional while training is Windows-only.
