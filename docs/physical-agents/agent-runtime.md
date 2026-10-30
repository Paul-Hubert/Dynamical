# Agent runtime

The agent runtime is the C++ replacement for the Unity `Agent` subclass, `DecisionRequester` and the
scenario MonoBehaviours. It lives in `src/agent/` (part of `dynamical_sim`), so the training host, the
game and the tests all run the same code.

The **content** — which observations, which reward terms, which terminations, which randomisation
ranges, which stats — is defined by the Unity plan's agent-runtime sections (§3.3–3.8) and must be
ported 1:1. This doc defines where each piece lives and the interfaces it implements.

## Components (on the agent root entity, next to `ArticulatedBody`)

```cpp
// src/agent/components.h
struct Agent {
    uint16_t behavior;            // index into the BehaviorRegistry
    int32_t group_id = 0;
};
struct AgentId { int32_t value; };                      // wire id, stable across episodes
struct ActionBuffer {
    std::vector<float> continuous;                      // last received, already in [-1, 1]
    std::vector<int32_t> discrete;
    bool fresh = false;                                 // set by the bridge, cleared by ActionApplySys
};
struct ObservationBuffer {
    std::vector<std::vector<float>> obs;                // one vector per observation spec (obs_0, obs_1, ...)
    std::vector<uint8_t> action_mask;                   // only if discrete branches exist
};
struct RewardAccumulator {
    float since_decision = 0.f;                         // sent and cleared every decision
    float episode = 0.f;                                // for stats
    float group_since_decision = 0.f;
    std::array<float, 16> terms_episode{};              // per-term breakdown for stats/ImGui
};
struct Episode {
    uint32_t step = 0;                                  // decisions in this episode
    uint64_t physics_ticks = 0;
    bool terminated = false;                            // done, not interrupted
    bool truncated = false;                             // max_step_reached = true
    uint32_t count = 0;                                 // episodes completed by this slot
};
struct Command {                                        // commanded-walker lines and the game
    glm::vec3 direction; float speed; glm::vec3 facing;
};
struct Heuristic {};                                    // tag: actions come from code, not the policy
```

An **agent slot** is created once (`buildAgent(reg, world, body, behavior)`) and lives for the whole
process. Episodes reset it in place; the `AgentId` never changes.

## Behaviour registry and specs

```cpp
// src/agent/behavior_spec.h
struct ObservationSpec {
    std::string name;                         // as Unity names the sensor, e.g. "VectorSensor"
    std::vector<int32_t> shape;               // e.g. {N}
    std::vector<int32_t> dimension_properties;// DimensionPropertyProto values; {0}=UNSPECIFIED for vectors
    int32_t observation_type = 0;             // DEFAULT=0, GOAL_SIGNAL=1
};
struct ActionSpec { int32_t num_continuous; std::vector<int32_t> discrete_branches; };
struct BehaviorSpec {
    std::string name;  int team;              // wire name "name?team=N"
    std::vector<ObservationSpec> observations;
    ActionSpec actions;
    std::string full_name() const;
};
struct BehaviorRegistry { std::vector<BehaviorSpec> behaviors; };   // host ctx, read-only after setup
```

Specs are built from the scenario's observation and action layouts at startup and are what the bridge
announces to Python ([mlagents-bridge.md](mlagents-bridge.md#brain-parameters)).

## Layout interfaces

```cpp
// src/agent/observation/observation_layout.h
class ObservationLayout {
public:
    virtual ~ObservationLayout() = default;
    virtual std::vector<ObservationSpec> specs() const = 0;
    // Writes every observation of one agent. Must be a pure function of the registry state:
    // the parity tests call it on poses loaded from Unity and compare element by element.
    virtual void write(const entt::registry&, entt::entity agent, ObservationBuffer&) const = 0;
};

// src/agent/actions/action_layout.h
class ActionLayout {
public:
    virtual ~ActionLayout() = default;
    virtual ActionSpec spec() const = 0;
    // Maps the [-1,1] action to joint targets/strengths. `tick_in_period` allows per-tick smoothing.
    virtual void apply(entt::registry&, entt::entity agent, const ActionBuffer&, uint32_t tick_in_period) const = 0;
    // The action that would hold the current pose (joint rotations -> normalised targets).
    // Used by the demo recorder; see demo-recorder.md.
    virtual std::vector<float> inverse(const entt::registry&, entt::entity agent) const = 0;
};

// src/agent/reward/reward_term.h
class RewardTerm {
public:
    virtual ~RewardTerm() = default;
    virtual const char* name() const = 0;
    virtual bool per_tick() const = 0;                  // true: RewardSys every tick; false: at decisions
    virtual float evaluate(const entt::registry&, entt::entity agent, double h) const = 0;
};

// src/agent/termination/termination_rule.h
class TerminationRule {
public:
    virtual ~TerminationRule() = default;
    virtual const char* name() const = 0;
    virtual bool check(const entt::registry&, entt::entity agent) const = 0;
    virtual float terminal_reward() const { return 0.f; }
};
```

Implementations register by name in a static table (`registerObservationLayout("humanbot_v3", …)`), and
the scenario file picks them. One file per Unity concept keeps the 1:1 port reviewable:

| Unity plan concept | C++ location |
|---|---|
| Observation layout (D3) | `src/agent/observation/humanbot_v3.cpp` |
| Action mapping (normalised targets → joint rotations, strengths) | `src/agent/actions/humanbot_v3.cpp` |
| Reward terms (imitation/GAIL-side terms, upright, velocity, energy, …) | `src/agent/reward/*.cpp`, one class per term |
| Terminations | `src/agent/termination/*.cpp` |
| Randomisation | `src/agent/randomisation/*.cpp` |
| Stats (incl. DeVita stats) | `src/agent/systems/stats.cpp` |
| Exact energy channel | `src/physics/systems/energy.cpp` + reward/obs terms reading it |

Shared helpers used by many observations go in `src/agent/observation/features.h`: COM position and
velocity, segment pose relative to a heading frame, joint angles normalised by limits, foot contacts,
target direction in the agent frame. Write these against `SegmentState`/`JointState` only.

## Systems

| System | List | Reads | Writes |
|---|---|---|---|
| `ActionApplySys` | fixed | `ActionBuffer`, `Command` | `JointTarget` |
| `RewardSys` | fixed | everything above, `EnergyState`, `SegmentContacts` | `RewardAccumulator` |
| `TerminationSys` | fixed | states, `Episode` | `Episode::terminated/truncated`, terminal reward |
| `ObservationSys` | fixed (decision ticks) | states | `ObservationBuffer` |
| `EpisodeSys` | fixed (decision ticks) | `Episode`, buffers | arena `Outbox`; resets slots |
| `StatsSys` | fixed (decision ticks) | `RewardAccumulator`, `Episode`, energy | arena `Outbox::stats` |

### Episode semantics (ML-Agents-compatible)

At each decision tick, for each agent, `EpisodeSys`:

1. Builds an `AgentStep { id, behavior, obs, reward = since_decision, done, max_step_reached, group_id, group_reward, action_mask }`
   and appends it to the arena's `Outbox`. `done = terminated || truncated`;
   `max_step_reached = truncated && !terminated`.
2. Clears `since_decision`.
3. If `done`: pushes episode stats, calls `resetAgent` (randomise → new pose → fresh obs), increments
   `Episode::count`. The new episode's **first** observation is emitted at the **next** decision tick,
   like Unity's `EndEpisode` + `DecisionRequester` path. A done agent receives no action for that step
   (the bridge knows this; see [mlagents-bridge.md](mlagents-bridge.md#action-routing)).
4. Increments `Episode::step`; sets `truncated` when it reaches `max_episode_steps`.

`resetAgent` order: sample randomisation → apply body changes (mass/shape) → ground changes (slope) →
set spawn pose (rest pose or a frame of a pose track) → `Ragdoll::ResetWarmStart` → `BodySyncSys` once
(so observations are valid) → clear energy, contacts, reward accumulators.

On the trainer's `RESET` command, every agent in every arena resets and emits an observation
immediately, with `reward = 0` and `done = false`.

### Outbox (arena → bridge)

```cpp
struct Outbox {                                 // in each arena's ctx; cleared by the bridge after gather
    std::vector<AgentStep> steps;               // in agent creation order (stable)
    std::vector<StatEntry> stats;               // {key, value, aggregation}
};
```

The bridge gathers outboxes in arena order, so the order of agents on the wire is deterministic.

## Environment parameters

`EnvParams` (host ctx) holds the latest float value per key received on the Environment Parameters side
channel (curriculum lessons, `environment_parameters` in the trainer YAML). Each arena reads it **at
episode start** only, so a change never alters an episode in progress, matching Unity's
`Academy.Instance.EnvironmentParameters.GetWithDefault(key, default)` usage in `OnEpisodeBegin`.

```cpp
float EnvParams::get(std::string_view key, float fallback) const;   // fallback = ScenarioDef default
```

Sampler-type parameters (uniform / gaussian / multi-range, sent with a seed) are resolved into a
`Sampler` and sampled with the arena RNG per episode.

## Randomisation

`Randomiser`s implement `void sample(ArenaRng&, const EnvParams&, EpisodeConfig&)`. `EpisodeConfig`
(per agent) holds everything an episode may vary: ground slope, spawn heading, spawn pose/frame,
body scale/mass factors, drive strength factors, target speed/direction, push schedule. The Unity plan
lists which of these exist and their ranges.

RNG: `ArenaRng` = `std::mt19937_64` seeded with `hash(base_seed, arena_index)`; `base_seed` comes from the
trainer's initialisation message (`seed`) or `--seed`. Never use `rand()` (the existing AI does; agent
code must not).

## Stats

Per-episode stats (episode length, reward per term, energy per metre, speed, cadence, DeVita metrics…)
go out on the Stats side channel as `{key, value, aggregation}`; ML-Agents logs them to TensorBoard /
W&B under the behaviour's run. Keys keep the Unity names so dashboards keep working.
Per-step stats (e.g. instantaneous power) use `MOST_RECENT` or are averaged in C++ before sending to
keep the side channel small.

## Heuristic agents

`Heuristic`-tagged agents get actions from a C++ function (zero action, random, scripted, or pose-track
targets) instead of the trainer. Used by `--mode bench`, by the demo recorder and for debugging in the
viewer.
