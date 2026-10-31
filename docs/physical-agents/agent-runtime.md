# Agent runtime

The C++ replacement for the ~5.7k lines of active-path MonoBehaviours (UP §3.3). The **content** is
UP's: layouts in §3.4–3.5, reset, randomisation, rewards and termination in §3.7, energy in §3.8. This
doc defines where each piece lives and the interfaces it implements.

## Components (agent root entity, next to `ArticulatedBody`)

```cpp
struct Agent        { uint16_t behavior; int32_t group_id = 0; };
struct AgentId      { int32_t value; };                 // stable across episodes
struct ActionBuffer { std::vector<float> continuous; bool fresh = false; };
struct HeldDrive    { /* per joint: spring, damper, target local rotation, target angular velocity */ };
struct ObsStack     { std::vector<float> frames; uint32_t size, depth; };   // oldest first, zeroed at reset
struct RewardWindow { float reward = 0; std::array<float, 16> terms{}; };
struct Episode {
    uint64_t tick = 0;                                  // ticks since episode start
    uint32_t decisions = 0;
    bool killed = false, truncated = false;
    bool parked = false;                                // terminal snapshot taken, waiting for window end
    std::vector<float> terminal_obs; float terminal_reward = 0;
};
struct TaskState    { glm::vec3 target; float target_speed; uint32_t arc_ticks; /* push/pull state */ };
struct AgentConfig  { std::shared_ptr<const AgentParams> params; EpisodeOverrides overrides; };
```

An **agent slot** is built once by `AgentFactory` (world init, from `AgentSpawn`). Episodes reset it
in place.

## Layouts

```cpp
class ObservationLayout {           // one per (body, version), registered by name
public:
    virtual std::vector<ObservationSpec> specs() const = 0;     // names exactly as Unity (UP §3.4)
    virtual std::span<const char* const> element_names() const = 0;
    virtual void write(const entt::registry&, entt::entity agent, std::span<float> out) const = 0;   // pure
};
class ActionLayout {
public:
    virtual int size() const = 0;
    virtual void apply(entt::registry&, entt::entity agent, std::span<const float> a) const = 0; // gains + targets
    virtual void zero(std::span<float> a) const;                 // demo actions are zeros (UP §3.10)
};
```

- **The parity layout** (`heidelbot_notoes_1`, D4) follows UP §3.4 with the noToes joint list (15 joints: UP's 17 without `RToeBase` and `LToeBase`)
  bit for bit:
  - the 3-float header through the **non-orthogonal** spine frame;
  - per joint, the normal and tangent negated on right-side joints, and the bone finite-difference
    velocity, all in the parent joint's frame;
  - the UP joint order;
  - a stack of 2, with the sensor name `StackingSensor_size2_VectorSensor_size138` (3 + 15 × 9).
- Optional blocks (`targetSpeedObs`, `dyingObs`, per-joint channels, the `nogail` goal sensor) are
  flags of the layout, set from the params file only.
- **The action layout** follows UP §3.5 (per joint: spring, damper, rx, ry, rz, ωx, ωy, ωz):
  - gains `(a + 1)/2` times the factors, held until the next decision, initialised to (0.5, 0.5);
  - `rx` mapped onto the twist range, centred;
  - `ry` and `rz` as fractions of the swing limits;
  - ω times `maxTargetAngularVelocity`.

  There is no clipping in C++; the trainer clips.
- Shared math lives in `src/agent/features.h` (`UnorthogonalReferential`, horizontal projection,
  bone finite differences) and is written against `SegmentState` / `JointState` only.
- **Fixes are versions.** Fixing the header leak or the right-side negation creates a new layout
  version. A new version needs new demos and a new baseline (UP D3).

## Systems (world fixed lane, order in [simulation.md](simulation.md#per-tick-schedule))

| System | Ports (UP §3.3) | Notes |
|---|---|---|
| `AgentFactory` (init) | `JointAgent` setup, `RigidbodyCopy` | Builds ragdoll, joint order, collision group |
| `TrainerActions` / `Policy` / `TrackActions` | `BotJoint` action mapping | Writes `HeldDrive` via `ActionLayout::apply` |
| `DriveTargets` | `BotJoint` drive configuration | Jolt motors from `HeldDrive`, every tick |
| `Disturbances` | `HumanBotAgent` push/pull | Push impulse on a random segment, scaled by total mass; pull = temporary motorised constraint to a static anchor ≤ 2 s |
| `Task` | `SetTarget`, `ArcUpdate`, `TargetMover`, `RandomSpeed` | Target re-placed every `arcFrames` ticks (not physics-rate scaled) |
| `Rewards` | `HumanBotAgent.CalculateRewards`, `EnergyRewardProvider` | Terms as classes with names = stat names; legacy rewards only if a config needs them |
| `Termination` | dying/kill, `maxFrames` | Kill pays `deathPenalty`, terminal done; truncation is `max_step_reached`, win reward if alive |
| `Observation` | `CollectObservations` | Pushes into `ObsStack` |
| `Episode` | `BotAgent` lifecycle | Outbox, then reset in UP §3.7 order |
| `Stats` | stat recording, `DeVitaStepStat` (M7) | Same stat names (UP §3.9) |

**Reward gating.** `AddReward × rewardFactor` applies only while training (UP §3.3). The `training`
flag comes from the brain parameters `is_training` and from the mode.

## Reset order (UP §3.7)

1. `RandomSpeed`.
2. Re-read env params; a config `targetSpeed` overwrites the draw.
3. Rest pose, zero velocities, `ResetWarmStart`.
4. `SetTarget`.
5. Optional random yaw. Then, with probability `randomInitChance`, the random-init drop.
6. Reset the reward providers.
7. `BodySync` once, then clear the finite-difference history, `ObsStack`, energy and contacts.

Randomisation is gated by `randomize && training && !recording`. Use the per-world RNG
(`WorldRng`), seeded from `seed + worker_id` and the world index. Reproducing Unity's exact sequence
is neither possible nor needed.

## Outbox

```cpp
struct AgentStep { int32_t id; uint16_t behavior; std::span<const float> obs; float reward; bool done, max_step_reached; int32_t group_id; float group_reward; };
struct StatEntry { std::string key; float value; uint8_t aggregation; };   // 0..3, includes Histogram
struct Outbox    { std::vector<AgentStep> steps; std::vector<StatEntry> stats; };
```

- One per world. `TrainHost` gathers outboxes in world index order.
- Stats are emitted only while training (UP §3.9).
