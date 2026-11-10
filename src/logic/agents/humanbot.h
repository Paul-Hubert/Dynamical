#ifndef DY_HUMANBOT_H
#define DY_HUMANBOT_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "humanbot_assets.h"
#include "logic/systems/system.h"

namespace dy {

class PhysicsWorld;

// The HumanBot walker on Jolt (docs/physical-agents/agent-runtime.md): one agent per world, its state in the world
// context variable HumanBot, created by AgentFactory from the HumanBotSpawn component

constexpr int humanbot_joints = 15;
constexpr int humanbot_frame = 138;             // 3 header + 9 x 15
constexpr int humanbot_stack = 2;
constexpr int humanbot_observations = humanbot_frame * humanbot_stack;
constexpr int humanbot_actions = 8 * humanbot_joints;

// Zero for a zero vector, as Unity's .normalized (glm::normalize gives NaN)
inline glm::vec3 safeNormalize(glm::vec3 v) {
    float l = glm::length(v);
    return l > 1e-6f ? v / l : glm::vec3(0.f);
}
inline glm::vec3 horizontal(glm::vec3 v) { return glm::vec3(v.x, 0.f, v.z); }

// The scene entity asking for an agent. motion: "dynamic" or "kinematic" (rig_anim, the demo recorder)
struct HumanBotSpawn {
    std::string body, params;
    bool kinematic = false;
    bool recording = false;                     // Unity's p.recording: header x, z zeroed, no randomisation or death
    bool draw = false;                          // WorldShape entities for WorldView
    bool view = false;                          // walker_view: no randomisation, a fixed command
    bool view_truncate = false;                 // ... with episodes truncated as in training (eval)
    float view_speed = 1.36f;
    int max_recording_ticks = 1000;
};

// One shape of a world body drawn by WorldView; its Transform (and PhysicsPose) are written by BodySync
struct WorldShape {
    enum class Type : uint8_t { Box, Sphere, Capsule } type = Type::Box;
    glm::vec3 half_extent = glm::vec3(0.5f);    // as RigidBody::half_extent
    int segment = 0;
    glm::vec3 local_position = glm::vec3(0.f);
    glm::quat local_rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
};

// World state of every segment, the observation code's input
struct RagdollState {
    std::array<glm::vec3, 16> position, linear, angular;
    std::array<glm::quat, 16> rotation;
    std::array<glm::vec3, humanbot_joints> anchor_previous, anchor_velocity;
    bool history_valid = false;
};

// Decision snapshot: the command the last observation carried, against which the window is rewarded
struct DecisionSnapshot {
    glm::vec3 spine = glm::vec3(0.f);
    glm::vec3 tsv = glm::vec3(0.f);
    float target_speed = 0.f;
};

// One joint's work during the last physics tick (the exact Jolt channel)
struct JointWork {
    glm::vec3 torque = glm::vec3(0.f);          // the motor's, N m, world
    glm::vec3 omega = glm::vec3(0.f);           // relative angular velocity at mid step
    float work = 0.f;                           // J
};

// Energy books per physics tick (Energy system) and per decision window (the reward)
struct EnergyBooks {
    bool valid = false;
    std::array<glm::vec3, 16> prev_linear, prev_angular;
    std::array<glm::quat, 16> prev_rotation;
    std::array<JointWork, humanbot_joints> joints;
    std::array<glm::vec3, 16> contact_impulse;  // N s, from the momentum balance
    double energy = 0.;                         // whole-body kinetic + potential, J
    float tick_pos = 0.f, tick_neg = 0.f, tick_motor = 0.f, tick_contact = 0.f, tick_energy = 0.f, tick_residual = 0.f;
    float window_pos = 0.f, window_neg = 0.f;
    std::array<glm::vec3, humanbot_joints> prev_torque;
    bool prev_torque_valid = false;
    float tick_torque_variation = 0.f, window_torque_variation = 0.f;   // sum over joints of |torque change|, N m
    double cum_abs_work = 0., cum_residual = 0., cum_net_residual = 0.;
};

// One limb's stance window (Unity's DeVitaStepStat): it opens at the first loaded tick and closes at the last, the
// debounce only decides whether a run of ticks counts. W is what is integrated over the window, with operator+=
template <class W>
struct StanceWindow {
    bool in_stance = false;
    int on = 0, off = 0, start_tick = 0;
    W stance {}, pending_on {}, pending_off {};
    int stance_ticks = 0, pending_on_ticks = 0, pending_off_ticks = 0;

    template <class Add, class Emit>
    void step(bool loaded, int tick, float dt, int debounce, float max_seconds, Add add, Emit emit) {
        if(loaded) { on++; off = 0; } else { off++; on = 0; }
        if(!in_stance) {
            if(!loaded) { pending_on = W {}; pending_on_ticks = 0; return; }
            add(pending_on);
            pending_on_ticks++;
            if(on < debounce) return;
            in_stance = true;
            stance = pending_on;
            stance_ticks = pending_on_ticks;
            start_tick = tick - (stance_ticks - 1);
            pending_on = W {}; pending_off = W {};
            pending_on_ticks = pending_off_ticks = 0;
            return;
        }
        if(loaded) {
            stance += pending_off;
            stance_ticks += pending_off_ticks;
            pending_off = W {};
            pending_off_ticks = 0;
            add(stance);
            stance_ticks++;
        } else {
            add(pending_off);
            pending_off_ticks++;
            if(off >= debounce) {
                emit(stance, stance_ticks, start_tick);
                in_stance = false;
                stance = W {}; pending_off = W {};
                stance_ticks = pending_off_ticks = 0;
                return;
            }
        }
        if(stance_ticks * dt > max_seconds) {
            in_stance = false;
            stance = W {}; pending_off = W {};
            stance_ticks = pending_off_ticks = 0;
        }
    }
};

struct SagittalWork {
    float pos = 0.f, neg = 0.f;
    SagittalWork& operator+=(const SagittalWork& o) { pos += o.pos; neg += o.neg; return *this; }
};

// DeVita's protocol: this episode's stances, published at its reset; the totals run over every episode (eval)
struct DeVitaBooks {
    std::array<StanceWindow<SagittalWork>, 2> stance;
    float sum_pos = 0.f, sum_neg = 0.f;
    int stances = 0;
    double total_pos = 0., total_neg = 0.;
    int64_t total_stances = 0;
};

// Positive and negative work, J
struct WorkSum {
    double pos = 0., neg = 0.;
    void add(double work) { if(work >= 0.) pos += work; else neg -= work; }
    WorkSum& operator+=(const WorkSum& o) { pos += o.pos; neg += o.neg; return *this; }
};

// The energy formulas of a long rollout (docs/physical-agents/energy-formulas.md), over every settled tick and episode
struct FormulaTotals {
    int64_t ticks = 0, stances = 0;
    double seconds = 0., metres = 0.;
    double lagrangian_abs = 0.;                 // sum |dL|, L = sum (T - V)
    WorkSum lagrangian, body_energy;            // the same delta split by sign; T + V, Unity's body channel
    WorkSum rne, drive;                         // 3D, every joint, every tick, split per joint per tick
    WorkSum rne_sagittal, drive_sagittal;       // DeVita: sagittal, hip knee ankle of the stance limb, stance only
    WorkSum rne_stance_3d, drive_stance_3d;     // the same joints and windows, 3D

    // Verification: every sum of norms is over joints or segments and ticks
    double point_error = 0., point_reference = 0.;              // Newton-Euler joint force against Jolt's point lambda
    double torque_error_free = 0., torque_reference_free = 0.;  // Newton-Euler torque against the motor, no limit active
    double limit_torque = 0.;                                   // the same difference with a limit active: the limits' share
    int64_t joint_ticks_free = 0, joint_ticks_limit = 0;
    double contact_error = 0., contact_reference = 0.;          // Jolt's contact impulse against the momentum balance
    double root_linear = 0., root_angular = 0., weight_impulse = 0.;   // what Newton-Euler leaves on the root
    double closure_net = 0., closure_tickwise = 0., closure_gross = 0.;   // dE against joint work plus contact work
    double closure_point_net = 0., closure_point_tickwise = 0.;   // ... also counting the work of the point constraints
    double contact_work = 0.;
    double torque_variation = 0.;               // sum over joints and settled ticks of |motor torque change|, N m
    int64_t clamped_ticks = 0;

    FormulaTotals& operator+=(const FormulaTotals& o);
};

struct FormulaStanceWork {
    WorkSum rne_sagittal, drive_sagittal, rne_3d, drive_3d;
    FormulaStanceWork& operator+=(const FormulaStanceWork& o) {
        rne_sagittal += o.rne_sagittal; drive_sagittal += o.drive_sagittal; rne_3d += o.rne_3d; drive_3d += o.drive_3d;
        return *this;
    }
};

struct FormulaBooks {
    bool valid = false;
    std::array<glm::vec3, 16> prev_position, prev_linear, prev_angular;
    std::array<glm::quat, 16> prev_rotation;
    double prev_lagrangian = 0., prev_energy = 0.;
    std::array<StanceWindow<FormulaStanceWork>, 2> limb;
    std::vector<int> deepest_first;             // joints, children before parents
    FormulaTotals totals;
};

struct HumanBot {
    std::shared_ptr<const BodyDef> body;
    std::shared_ptr<const AgentParams> base_params;
    AgentParams params;                         // base_params + env params, refreshed each episode
    bool kinematic = false, recording = false, draw = false, view = false, view_truncate = false;
    float view_speed = 1.36f;
    int recording_ticks = 1000;                 // episode length while recording
    bool built = false;

    std::vector<JPH::BodyID> bodies;
    std::vector<JPH::Ref<JPH::SwingTwistConstraint>> constraints;
    // Constraint space to body 1 (parent) and body 2 (child) rotation, X twist, Y plane, Z normal. Not Jolt's: its
    // Y is the normal and its Z the plane axis. Motor axes come from GetConstraintToBody2
    std::vector<glm::quat> frame1, frame2;
    float built_mass = 0.f;

    RagdollState state;
    float est0 = 1.f;

    glm::vec3 target = glm::vec3(0.f);
    float target_speed = 0.f;
    bool continuous_speed_mode = false;
    DecisionSnapshot snapshot;

    std::array<float, humanbot_frame> previous_frame {};  // the older half of the stack

    int tick = 0, decisions = 0, window_ticks = 0, arc_ticks = 0, dying_ticks = 0;
    bool dying = false, killed = false, truncated = false, pending_reset = false, fresh = false;
    double cumulative_reward = 0.;
    uint64_t target_clamps = 0;

    EnergyBooks books;
    DeVitaBooks devita;
    FormulaBooks formulas;
    int left_hip = -1, left_knee = -1, left_ankle = -1, right_hip = -1, right_knee = -1, right_ankle = -1;  // joints
    // Unity's normaliser maxima and extremes, kept for the same stat names
    float head_max_speed = 0.f, head_max_error = 0.f, head_max_height = 0.f, head_min_height = 10.f;
    float root_max_speed = 0.f, root_max_error = 0.f, root_max_distance = 0.f;
    float slope_angle = 0.f;                    // params default for the slope
    float slope_applied = 0.f;                  // this episode's angle, degrees
    float energy_speed = 0.f;                   // smoothed horizontal speed of the energy reward
    bool energy_speed_valid = false;

    float currentHeight() const;                // Unity's EstimateHeight: head above the higher foot, on bone origins
    bool isDying() const { return currentHeight() < params.dying_height_factor * est0; }
};

// Unity's yaw-only heading frame of a segment (agent-runtime.md): x forward, y up, z = x cross y
struct HeadingFrame {
    glm::vec3 x, y, z;
    glm::vec3 project(glm::vec3 v) const { return glm::vec3(glm::dot(x, v), glm::dot(y, v), glm::dot(z, v)); }
};
HeadingFrame headingFrame(glm::quat rotation);

// The layout heidelbot_notoes_2 for the current state: 3 header + 9 per joint
void observeV2(const HumanBot& bot, float* out);

// A new episode: resolved parameters, target speed, spawn pose, target, randomisation; the observation history reset
void resetHumanBot(entt::registry& reg);

// World's resource path: relative to the working directory, else to the executable
std::string resourceFile(const std::string& relative);

class AgentFactorySys : public System {
public:
    AgentFactorySys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AgentFactory"; }
    void preinit() override;
    void init() override;
    void tick(double h) override {}
    void finish() override;
};

// Act phase: the window's action as motor targets and gains
class DriveTargetsSys : public System {
public:
    DriveTargetsSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "DriveTargets"; }
    void tick(double h) override;
};

class DisturbancesSys : public System {
public:
    DisturbancesSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Disturbances"; }
    void tick(double h) override;
};

// After PhysicsStep: segment states, finite-difference anchor velocities, the drawn shapes
class BodySyncSys : public System {
public:
    BodySyncSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "BodySync"; }
    void tick(double h) override;
};

// The target re-placement (Unity's ArcUpdate)
class HumanBotTaskSys : public System {
public:
    HumanBotTaskSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Task"; }
    void tick(double h) override;
};

// Kill and truncation. A kill parks the world until the window ends
class TerminationSys : public System {
public:
    TerminationSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Termination"; }
    void tick(double h) override;
};

// Observe phase, in this order: Rewards against the snapshot, Observation (new snapshot, stack), Episode (done, parking)
class RewardsSys : public System {
public:
    RewardsSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Rewards"; }
    void tick(double h) override;
};

class ObservationSys : public System {
public:
    ObservationSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Observation"; }
    void tick(double h) override;
};

class EpisodeSys : public System {
public:
    EpisodeSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Episode"; }
    void tick(double h) override;
};

// After BodySync: the energy books of the step just done
class EnergySys : public System {
public:
    EnergySys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Energy"; }
    void tick(double h) override;
};

// DeVita's protocol (DeVita/*): per stance, sagittal work per joint of one limb, split per joint by sign; the ratio of
// sums over an episode
class DeVitaStepStatSys : public System {
public:
    DeVitaStepStatSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "DeVitaStepStat"; }
    void tick(double h) override;
};

// The three energy formulas of a long rollout, eval only (view_truncate): after BodySync
class EnergyFormulasSys : public System {
public:
    EnergyFormulasSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "EnergyFormulas"; }
    void tick(double h) override;
};

// Tilts the ground for this episode (SlopeGround) from the env params
void applySlope(entt::registry& reg, HumanBot& bot);

void registerHumanBotEnergy();

// Kinematic track playback (rig_anim, the demo recorder): MoveKinematic toward each frame in turn
struct PoseTrackPlayer {
    std::string track;
    bool loop = false;
    bool teleport = false;
    bool playing = true;
    int frame = 0;
    std::shared_ptr<const PoseTrack> data;
};

class TrackActionsSys : public System {
public:
    TrackActionsSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "TrackActions"; }
    void init() override;
    void tick(double h) override;
};

void registerHumanBot();

}

#endif
