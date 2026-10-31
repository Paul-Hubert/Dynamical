# Physics

The target is **UP's contract, not PhysX's dynamics.** Match what the experiment depends on: limits,
ranges, frames, mass properties and the energy readout. Accept and document the rest (UP §3.2). The
Jolt doc owns the solver settings and the energy recorder.

## Changes to master's Jolt code

Master has `PhysicsWorld` (`src/physics/physics_world.*`), `PhysicsSys` (`physics_sys.*`) and
`TerrainColliders`.

| # | Change | Why |
|---|---|---|
| P1 | **`JoltRuntime`** RAII guard in `main`, `train_main` and the test runner. It owns `RegisterDefaultAllocator`, `Trace`, `AssertFailed`, `Factory::sInstance` and `RegisterTypes`/`UnregisterTypes`, which today run in `PhysicsWorld`'s constructor and destructor | With two worlds, the first destructor unregisters types the second still uses |
| P2 | **`PhysicsConfig`**: `threads` (1 → `JobSystemSingleThreaded`, else `JobSystemThreadPool`), `max_bodies`, `max_body_pairs`, `max_contacts`, `temp_bytes`, `gravity`, `PhysicsSettings` overrides (velocity and position steps, speculative distance, penetration slop, restitution threshold), `collision_steps` | Training worlds are small (≈ 32 bodies) and single-threaded. `base` keeps 65536 bodies, the thread pool and a 16 MB temp allocator |
| P3 | `PhysicsWorld` lives in the **registry ctx** | Builder, drive, contact and energy systems need it; `PhysicsSys` stops owning it |
| P4 | **Layers** become `STATIC`, `MOVING`, `AGENT`, `SENSOR` (broad-phase `NON_MOVING`, `MOVING`) | Agents collide with arena geometry only (UP §3.2: no self-collision, no agent–agent). Self-collision is handled per ragdoll by a group filter |
| P5 | **Split `PhysicsSys`** into `RigidBodySys` (create and destroy `RigidBody` bodies), `TerrainCollidersSys`, and `PhysicsSys` itself (accumulator, interpolation; used by `base`). Agent worlds use `PhysicsStep` (one step per fixed tick) | `base` keeps its behaviour; worlds get the fixed lane |
| P6 | `RigidBody::Shape::Capsule`; `Ground` component | Flat ground and debug content in worlds |
| P7 | A contact listener that records into a per-world queue | `Contacts` system; the base scene does not install it |

## Ground

**Flat only until M6 (plan E4).**
- `Ground { type = box, size, top_y = 0, material }`: a static `BoxShape` on `STATIC`, top face at
  y = 0.
- Training mirrors Unity's shared FlatFloor; DeVita's arena is a 150 × 10 × 150 box, also flat until
  slope (UP §3.1).
- `OptimizeBroadPhase()` runs once after the ground is added.

**Later.**
- M7, `SlopeGround`: rotates the static box about the spawn point per episode and rescales the target
  speed (UP §3.7).
- M9, heightfields: from heights exported by Unity (`Mathf.PerlinNoise` is Unity's own), not from
  FastNoise2 (UP §4 M9).

## Ragdoll build

`RagdollBuilder::build(reg, world, const BodyDef&, Motion motion)` returns the agent root entity.
`motion` is `Dynamic` for training and viewing, `Kinematic` for `rig_anim` and the demo recorder.

1. **Shapes per segment** from the body file (UP §3.1 inventories gladiator's 18 capsules + 6 boxes; the noToes export has no toe segments).
   - Several shapes on one segment become a `StaticCompoundShape`.
   - Unity capsule height includes both caps and runs along X, Y or Z. Jolt capsules use the
     half-height of the cylinder along local Y, so convert and rotate.
   - Box half-extents as exported.
   - Shapes are placed relative to the **bone origin**, which is the body frame in Unity, not the
     centre of mass.
2. **Mass properties exported, never recomputed** (UP §3.1):
   - `mOverrideMassProperties = MassAndInertiaProvided`, with the mass from the file;
   - inertia `R · diag(I) · Rᵀ`, with `R = inertiaTensorRotation`, about the exported centre of mass;
   - a `OffsetCenterOfMassShape` puts Jolt's centre of mass where Unity's was.
3. **Body settings**:
   - damping and maximum angular velocity set explicitly from the body file (UP §3.2: Unity caps
     at 20 rad/s in code, while Jolt's default is about 47);
   - sleeping off;
   - `LinearCast` motion quality (Unity CCD ContinuousDynamic);
   - layer `AGENT`, collision group `{ragdoll filter, agent group, segment index}`.
4. **Constraints** (next section). The parent of each joint is given by the body file; the anchor is
   the child bone origin, and the connected anchor is the runtime value Unity computed (exported).
5. **Self-collision off entirely** (UP §3.2): a `GroupFilterTable` disabling every pair, not just
   parent–child.
6. `RagdollSettings::Stabilize()` is **not** called: it changes masses.
7. **Entities**:
   - root: `ArticulatedBody`, plus `Agent` from the agent factory;
   - one per segment: `PhysicsBody`, `Segment`, `SegmentState`;
   - one per joint: `JointRef`, `JointState`, `JointTarget`.

**Reset without rebuilding** (UP §3.2):
- set every body's pose to the rest pose (or a track frame) and zero velocities;
- `ResetWarmStart()`;
- clear contacts, energy and finite-difference history.

Rebuild only when randomisation changes shapes.

## Joints

**Limits** use a `SwingTwistConstraint` in `LocalToBodyCOM` space, built from Unity's
`ConfigurableJoint` data:

| Unity | Jolt |
|---|---|
| `axis` (joint X) | twist axis |
| `secondaryAxis` (joint Y) | plane axis; normal = X × Y |
| low/high angular X limit | twist min/max. **The sign is easy to invert; the limit sweep decides** |
| angular Y and Z limits (symmetric, elliptical cone in PhysX) | plane and normal half-cone angles, `ESwingType::Cone` (elliptical) |
| hard limits: no spring, no bounce, contactDistance 0 (UP §3.1) | Jolt's default hard limits |
| connected bodies never collide | covered by the group filter |

Forearm axes are (0, 0, ∓1) and the arms are left/right asymmetric (UP §3.1). Take them as exported;
never symmetrise.

**Drives: SwingTwist motors.**
- Position mode for twist and swing.
- Stiffness and damping per window from the gain actions (UP §3.5).
- Torque limit from the body file.
- A target-velocity term from the ω actions, with the sign defined sanely: Unity's minus is not
  reproduced (UP §3.2).
- The target is a plain "local rotation relative to rest". Unity's inverse ZXY joint-space
  `targetRotation` is not reproduced.

**SLERP ≠ swing-twist motors.** The gain scale is **re-derived** in M2 from step-response tests, not
copied (UP §3.2).

## Contacts and friction

- The friction combine callback is `max`; restitution combine is `min` (UP §3.1: `HeidelBot`
  material friction 1, combine Max, so friction is effectively 1 everywhere).
- **Contacts system**: per segment it records touching ground, the contact point, and an impulse
  estimate.
  - Total ground reaction force comes from momentum; this is exact (Jolt doc §8).
  - Stance detection needs per-foot attribution.
  - Whether Jolt exposes post-solve impulses is open (UP §3.2). Check the Jolt version in vcpkg
    before relying on it.
- Log foot penetration as a stat: speculative contacts differ from PhysX's contact offset.

## Energy

`Energy` implements the Jolt doc's recorder (UP §3.8):
- per sub-step, motor and limit impulses per joint, giving exact positive, negative and absolute work;
- whole-body d(KE + PE) as a cross-check.

The new channel gets a **new** `energySource` value. Source 2 is not reused, and the tuned constants
(`energyScale`, Margaria multipliers, gate widths) are re-derived, not carried over.

M2 checks (Jolt doc §8):
1. contact work is 0 in flight phase;
2. ground reaction force from momentum;
3. energy residual under 1–2 %.

## Solver settings

From the Jolt doc's settings table, recorded in the agent parameter file and applied through
`PhysicsConfig`. They are tuned for convergence, not for parity with Unity's TGS 20/20 (UP §3.2).

## Jolt build

- vcpkg `joltphysics` 5.5 is already in use. The consumer's compile definitions must match the port
  (`JPH_*` flags propagated by `Jolt::Jolt`).
- Cross-platform determinism (`CROSS_PLATFORM_DETERMINISTIC`) needs an overlay port if the stock port
  does not expose it. It is optional while training is Windows-only (UP D5).
