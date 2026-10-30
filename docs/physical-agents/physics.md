# Physics (Jolt)

Jolt ≥ 5.0 replaces PhysX. The target is not "a ragdoll in Jolt" but **Unity's contract**: the same
limits, drive response, contacts and energetics within the tolerances the Unity plan sets (M2 gate).
Every PhysX default that Unity applies silently must be made explicit in the body file and reproduced
here; the parity checklist at the end lists them.

## Process-wide initialisation

```cpp
// src/physics/jolt_init.cpp — called once from main() of every target, before any PhysicsWorld exists
void initJolt() {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = &joltTrace;                       // forwards to dy::log
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &joltAssert;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}
void shutdownJolt() {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}
```

Use an RAII guard (`JoltRuntime`) in `main`, `train_main` and the test runner.

## `PhysicsWorld` (one per arena, or one in the game)

Stored in the registry ctx: `reg.set<PhysicsWorld>(config)`.

```cpp
// src/physics/physics_world.h
struct PhysicsConfig {
    uint32_t max_bodies = 1024, max_body_pairs = 4096, max_contact_constraints = 4096;
    uint32_t velocity_steps = 10, position_steps = 2;   // set from the body/scenario file
    int collision_steps = 1;
    glm::vec3 gravity = {0.f, -9.81f, 0.f};
    bool multithreaded = false;                         // training: false; game: true
};

class PhysicsWorld {
public:
    explicit PhysicsWorld(const PhysicsConfig&);
    void step(float h);                                  // PhysicsSystem::Update(h, collision_steps, ...)
    JPH::PhysicsSystem& system();
    JPH::BodyInterface& bodies();                        // the *non-locking* interface: single owner thread
    ContactQueue& contacts();
private:
    std::unique_ptr<JPH::TempAllocatorImpl> temp;        // 10 MB
    std::unique_ptr<JPH::JobSystem> jobs;                // JobSystemSingleThreaded or JobSystemThreadPool
    BroadPhaseLayers bp_layers; ObjectVsBroadPhase ovb; ObjectPairFilter pairs;
    JPH::PhysicsSystem physics;
    ContactRecorder listener;                            // JPH::ContactListener
};
```

- Training uses `GetBodyInterfaceNoLock()`: each world is touched by exactly one task at a time.
- `PhysicsSettings`: copy `mNumVelocitySteps` / `mNumPositionSteps` from the Unity solver iteration
  settings recorded in the body file; set `mAllowSleeping` off on agent bodies.
- After adding static ground, call `OptimizeBroadPhase()` once.

### Layers (`src/physics/layers.h`)

| Object layer | Broad-phase layer | Collides with |
|---|---|---|
| `STATIC` (ground, walls, props) | `NON_MOVING` | `AGENT`, `DYNAMIC` |
| `AGENT` (ragdoll segments) | `MOVING` | `STATIC`, `AGENT`*, `DYNAMIC` |
| `DYNAMIC` (balls, strike targets) | `MOVING` | everything |
| `SENSOR` (trigger volumes) | `MOVING` | `AGENT` |

\* Self-collision inside one ragdoll is controlled by a `JPH::GroupFilterTable` per body (see below),
not by layers. Agents of different arenas never meet because they are in different worlds.

## Components

```cpp
// src/physics/components.h
struct PhysicsBody   { JPH::BodyID id; };                 // any entity backed by a Jolt body
struct BodySegment   { entt::entity owner; uint16_t index; };   // index into BodyDef::segments
struct SegmentState  {                                    // sim frame, written by BodySyncSys
    glm::vec3 pos, com, lin_vel, ang_vel; glm::quat rot;
    glm::vec3 prev_pos; glm::quat prev_rot;               // for render interpolation
};
struct JointRef      { JPH::Ref<JPH::SwingTwistConstraint> constraint; entt::entity parent, child; uint16_t index; };
struct JointState    { glm::quat local_rot; glm::vec3 angles; glm::vec3 rel_ang_vel; glm::vec3 applied_torque; };
struct JointTarget   { glm::quat target_local; float strength = 1.f; };   // written by ActionApplySys
struct ArticulatedBody {                                  // on the agent root entity
    const BodyDef* def;
    JPH::Ref<JPH::Ragdoll> ragdoll;
    std::vector<entt::entity> segments;                  // by segment index
    std::vector<entt::entity> joints;                    // by joint index
};
struct Ground {};                                         // tag on static ground entities
```

Body user data (`BodyCreationSettings::mUserData`) stores the segment's `entt::entity` (as `uint64_t`),
so contact callbacks can map `BodyID → entity` without a lookup table.

## Building a body from the body file

`RagdollBuilder::build(reg, world, const BodyDef&, const Pose& spawn)` → root entity.

1. **Skeleton.** `JPH::Skeleton` with one joint per segment, names and parent indices from `BodyDef`.
2. **Parts.** For each segment, a `RagdollSettings::Part` (a `BodyCreationSettings`):
   - Shape from the collider list (capsule, box, sphere; several colliders → `StaticCompoundShape`).
     Unity capsules are defined by height (total, including caps) and radius along a local axis;
     Jolt capsules by half-height of the cylinder part along local Y — convert and rotate.
   - Centre of mass: wrap in `OffsetCenterOfMassShapeSettings` so the COM equals Unity's
     `Rigidbody.centerOfMass` (exported, not recomputed).
   - Mass and inertia: `mOverrideMassProperties = MassAndInertiaProvided`; mass = `Rigidbody.mass`;
     inertia = `R · diag(inertiaTensor) · Rᵀ` with `R = inertiaTensorRotation`.
   - `mLinearDamping = drag`, `mAngularDamping = angularDrag`, `mMaxAngularVelocity =
     maxAngularVelocity` (**Unity defaults this to 7 rad/s; Jolt to ~47 rad/s**).
   - `mMotionQuality = LinearCast` only if the Unity body used continuous collision detection.
   - `mObjectLayer = AGENT`, `mCollisionGroup = {group_filter, agent_group_id, segment_index}`.
3. **Constraints.** `Part::mToParent = SwingTwistConstraintSettings` built from the joint definition
   (next section).
4. **Self-collision.** `GroupFilterTable(num_segments)`: disable each parent–child pair
   (Unity `enableCollision = false`, the default), plus any pair the Unity layer matrix disables
   (exported as `ignore_pairs`). `RagdollSettings::DisableParentChildCollisions()` covers the first part.
5. `settings->Stabilize()` **only if** the Unity body was not already stable — it changes masses, so it
   breaks parity. Default off; log a warning instead if mass ratios across joints exceed 1:10.
6. `CalculateBodyIndexToConstraintIndex()`, `CalculateConstraintIndexToBodyIdxPair()`,
   `CreateRagdoll(group_id, user_data, &system)`, `AddToPhysicsSystem(EActivation::Activate)`.
7. Create the entities: root (`ArticulatedBody`, `Agent` added later by the agent factory), one per
   segment (`PhysicsBody`, `BodySegment`, `SegmentState`) and one per joint (`JointRef`, `JointState`,
   `JointTarget`). Segment entities carry **no `Position`** — `MapManager` never sees them.

### Reset without rebuilding

Episode resets reuse the ragdoll: `Ragdoll::SetPose` (or per-body `SetPositionAndRotation` +
`SetLinearAndAngularVelocity` for poses with velocities), then `Ragdoll::ResetWarmStart()`, and clear the
energy and contact state. Rebuild only when randomisation changes **shapes** (segment lengths/radii);
mass and inertia changes go through `MotionProperties::SetMassProperties` on the existing body.

## Joints

### Limits: Unity `ConfigurableJoint` → `SwingTwistConstraint`

| Unity | Jolt `SwingTwistConstraintSettings` |
|---|---|
| `connectedBody` / body | body 1 = parent, body 2 = child |
| `anchor` / `connectedAnchor` (local) | `mSpace = LocalToBodyCOM`; `mPosition1/2` = anchors expressed relative to each body's COM |
| `axis` (primary, joint X) | `mTwistAxis1/2` |
| `secondaryAxis` (joint Y) | `mPlaneAxis1/2` (joint Z = X × Y is Jolt's normal axis) |
| `lowAngularXLimit` / `highAngularXLimit` | `mTwistMinAngle` / `mTwistMaxAngle` (Unity's X limit sign convention is inverted relative to rotation about `axis`; verify with the test below) |
| `angularYLimit` (symmetric, about joint Y) | half-cone angle about the plane axis |
| `angularZLimit` (symmetric, about joint Z) | half-cone angle about the normal axis |
| PhysX D6 swing limit (elliptical cone) | `mSwingType = ESwingType::Cone` |
| `xMotion/yMotion/zMotion = Locked` | implied (swing-twist is a ball joint) |
| locked angular axis | limit 0 on that axis, or a `HingeConstraint` / `FixedConstraint` when two or three axes are locked |
| limit `bounciness`, `contactDistance`, limit spring | not supported by swing-twist; must be 0/default in Unity or flagged in the parity report |

Both the axis-to-cone mapping and the X-limit sign are easy to get backwards. **Required unit test**:
for each joint, rotate the child to each limit in Unity (exported as `limit_probes` in the body file:
child local rotation at each limit) and assert the Jolt constraint is exactly at its limit there.

### Drives

The Unity plan defines the drive contract (spring, damper, max force, and how actions become target
rotations). Two implementations behind one `JointDriveSys` setting `drive_mode`:

**A. Jolt motors (default, implicit — like PhysX drives).**

```cpp
c.SetSwingMotorState(JPH::EMotorState::Position);
c.SetTwistMotorState(JPH::EMotorState::Position);
auto& m = c.GetSwingMotorSettings();                  // and GetTwistMotorSettings()
m.mSpringSettings.mMode      = JPH::ESpringMode::StiffnessAndDamping;
m.mSpringSettings.mStiffness = def.spring * target.strength;   // Unity positionSpring
m.mSpringSettings.mDamping   = def.damper;                      // Unity positionDamper
m.SetTorqueLimit(def.max_force);                                // Unity maximumForce
c.SetTargetOrientationCS(constraintSpaceTarget(target.target_local, def));
```

- Applied torque for energy: `c.GetTotalLambdaMotor() / h` (constraint space → rotate to world). This
  is the impulse the solver really applied, so the energy channel stays exact.
- Unity Slerp drive ≈ a single orientation target; Unity X/YZ drive with different twist and swing
  strengths maps to different twist/swing motor settings. Use the mode the Unity body uses.
- Unity's `targetRotation` is expressed in the joint's *starting* local frame and inverted; convert
  once in `constraintSpaceTarget` and cover it with a test (target = identity ⇒ rest pose).

**B. Explicit PD torques (for energy experiments, M7).**

```
τ = clamp(k · rotvec(q_target · q_currentᶦ) − c · ω_rel, max_force)
AddTorque(child, τ); AddTorque(parent, −τ)
```

Exact by construction, but explicit: stable only when `h · sqrt(k / I_min)` is small. Needs a higher
`physics_hz` than A for the same gains. Not used for parity with Unity.

## Contacts

`ContactRecorder : JPH::ContactListener` records `OnContactAdded` / `OnContactPersisted` /
`OnContactRemoved` into a per-world vector (`std::mutex`-guarded; free in single-threaded training,
needed in the game's thread pool). `ContactSys` drains it into per-segment `SegmentContacts`
(touching ground, touching another agent, impulse estimate). The agent runtime reads only
`SegmentContacts`, never Jolt directly.

**Friction and restitution.** PhysX combines per-material values with a combine mode (Unity default
*Average*); Jolt uses the geometric mean by default. Install
`PhysicsSystem::SetCombineFriction` / `SetCombineRestitution` implementing the Unity combine modes, with
per-body material values read from the body file (`dynamic_friction`, `bounciness`, combine modes).
Jolt has one friction coefficient; use Unity's dynamic friction and flag static ≠ dynamic in the parity
report.

## Ground

- **v1 (E3): boxes.** A large static `BoxShape` per arena on `STATIC`, top surface at y = 0; tilted
  arenas rotate it about x/z by the scenario's slope (randomised per episode → `SetRotation` on the
  static body, then `OptimizeBroadPhase` is not needed for a single body).
- **Later: terrain.** Build a `HeightFieldShape` from FastNoise2 (same generator as `MapGenerator`) or
  from map chunks: tile `level` → sim y, map x → sim x, map y → sim z. Respect Jolt's sample-count /
  block-size constraints (`HeightFieldShapeSettings`), give each tile type a friction via
  `mMaterials`. Confirm `Tile::level` units (metres?) in `MapGenerator` first.

## Determinism

- Build Jolt with `JPH_CROSS_PLATFORM_DETERMINISTIC` so Windows and NixOS give identical rollouts.
- Single-threaded stepping per arena, fixed creation order of bodies and constraints, seeded RNG per
  arena (`seed + arena_index`), no wall-clock input.
- A determinism test runs 1000 ticks with random actions twice and compares a hash of all
  `SegmentState`s.

## Energy

`EnergySys` integrates per joint and per tick, from `JointState::applied_torque` and
`JointState::rel_ang_vel` (child minus parent angular velocity, world frame):

```
P = τ · ω_rel
W_pos += max(P, 0) · h       W_neg += max(-P, 0) · h       W_abs += |P| · h
```

Which channels feed observations, rewards and stats, and how the metabolic model weights positive and
negative work, comes from the Unity plan's exact-energy section. M2's "Jolt energy checks" are:

1. Free fall with no drives: kinetic + potential energy constant to < 0.1% over 2 s (no damping).
2. Passive pendulum segment: energy decays only by the configured damping.
3. Driven joint holding a pose against gravity: `W_pos − W_neg` equals the change in mechanical energy
   plus damping losses, to the Unity plan's tolerance.

## Parity checklist (body file must make each explicit)

| PhysX/Unity default | Value | Jolt default |
|---|---|---|
| `Rigidbody.maxAngularVelocity` | 7 rad/s | ~47 rad/s |
| Solver iterations (position/velocity) | 6 / 1 | 2 / 10 (named the other way round in Jolt: position steps / velocity steps) |
| Friction combine | Average | geometric mean |
| Bounce threshold | 2 m/s | `mMinVelocityForRestitution` 1 m/s |
| Default contact offset | 0.01 m | `mSpeculativeContactDistance` 0.02 m, `mPenetrationSlop` 0.02 m |
| Sleep threshold | 0.005 | sleeping on by default (turn off for agents) |
| Drag / angular drag | 0 / 0.05 | 0 / 0.05 (formula differs slightly: check in M2) |
| Joint projection, preprocessing | per joint | none |
| Gravity | (0, −9.81, 0) | (0, −9.81, 0) |
