#ifndef DY_ANIMAL_AGENT_H
#define DY_ANIMAL_AGENT_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "logic/agents/ragdoll.h"
#include "logic/agents/walk_routes.h"
#include "logic/animals/animal_body.h"
#include "logic/animals/gait_dataset.h"
#include "logic/animals/gait_walk.h"
#include "logic/systems/system.h"

namespace dy {

class GroundField;

// The physical animals (docs/physical-agents/reports/animal-agents.md): one agent per world, an animal of the library
// as a ragdoll on Jolt, driven at its joints by a policy. What the human walker has (logic/agents/humanbot.h), for
// any body the generator makes: its systems beside the walker's, on the same ragdoll build, drives, AgentIO and
// bridge. World context variable AnimalBot, created by AnimalFactory from the AnimalSpawn component

// Observation: a header, then 9 values for the trunk and for each joint; two of those stacked
constexpr int animal_header = 7;
constexpr int animal_stack = 2;
constexpr int animal_actions_per_joint = 8;

// resources/agents/params/animal.params.json: what every species shares. Env params of the same names override
struct AnimalParams {
    std::string file_hash;
    int max_frames_60hz = 2000;             // an episode, in 60 Hz frames (33.3 s)
    float root_reward = 0.34f;              // per decision at the commanded speed along the commanded heading
    float death_penalty = 0.f;              // on top of a death step, whose own reward is dropped
    float life_reward = 0.f;
    float reward_factor = 1.f;
    bool die_on_fall = true;
    float fall_height = 0.55f;              // the trunk under this part of its standing height: fallen
    float touch_margin = 0.004f;            // heights of the animal: a body this close over the ground touches it
    bool random_yaw = true;                 // episodes start facing anywhere
    float soft_resistance = 3.f;            // soft ground: how hard the layer holds a body, in weights of that body per m/s sideways
    float soft_lift = 0.25f;                // ... and upwards, as a part of that
};

AnimalParams parseAnimalParams(const nlohmann::json& json);
std::shared_ptr<const AnimalParams> loadAnimalParams(const std::string& path);

// The scene entity asking for an animal
struct AnimalSpawn {
    std::string body;                       // a body file, or a species id (the library's bodies folder)
    std::string params = "agents/params/animal.params.json";
    std::string ranges = "agents/ranges/walk_basic.json";   // GaitRanges: the commands, and the styles of the demonstrations
    std::string gait = "walk";              // the clip the kinematic walk is made from: walk, trot or run
    bool kinematic = false;                 // the procedural walk played on the body (demonstrations)
    bool recording = false;
    bool draw = false;                      // WorldShape entities for WorldView
    bool view = false;                      // no end of episode by time
    bool view_truncate = false;             // ... episodes as in training (eval)
    float speed = 0.f;                      // > 0: this command, straight ahead, in speeds of the walk clip, instead of draws
    int recording_decisions = 300;          // per recorded walk
    float warmup = 2.f;                     // seconds walked before a walk is recorded (plus up to a cycle, drawn)
    float heading = -1.f;                   // >= 0: recorded walks start with this heading, degrees (0 looks down -z); else drawn
    float soft_depth = -1.f;                // >= 0: this depth of soft ground, in heights, instead of the ranges'
};

// How an episode ended, kept in eval
struct AnimalEpisode {
    int ticks = 0;
    bool fell = false;
    int cause = 0;                          // 1: the trunk too low, 2: a body on the ground
    int segment = -1;                       // the body that touched
    float distance = 0.f;                   // m the trunk covered
    float progress = 0.f;                   // m along the commanded heading, summed per decision
    float commanded = 0.f;                  // m the command asked for
    float heading_error = 0.f;              // mean |angle| between the trunk's heading and the command, radians
    float soft_depth = 0.f;
};

struct AnimalBot {
    std::shared_ptr<const AnimalBody> body;
    std::shared_ptr<const AnimalParams> base_params;
    AnimalParams params;
    GaitRanges ranges;
    std::string ranges_hash;
    AnimalSpawn spawn;
    bool kinematic = false, recording = false, draw = false, view = false, view_truncate = false;
    bool built = false;
    bool immortal = false;                  // the standing test: no end by falling
    bool limp = false;                      // ... its motors off

    RagdollParts parts;

    // World state of every segment
    std::vector<glm::vec3> position, linear, angular;
    std::vector<glm::quat> rotation;
    std::vector<glm::vec3> origin_previous, origin_velocity;    // finite differences of the segments' origins
    bool history_valid = false;

    // The command (logic/animals/gait_walk.h) and the one the last observation carried
    WalkCommand command;
    glm::vec3 snapshot_trunk = glm::vec3(0.f), snapshot_direction = glm::vec3(0.f, 0.f, -1.f);
    float snapshot_speed = 0.f;

    int frame_size = 0;                     // animal_header + 9 x segments
    std::vector<float> previous_frame;

    int tick = 0, decisions = 0, window_ticks = 0;
    bool killed = false, truncated = false, pending_reset = false, fresh = false;
    int fall_cause = 0, fall_segment = -1;
    double cumulative_reward = 0.;
    uint64_t target_clamps = 0;
    float soft_depth = 0.f;                 // this episode's, m

    // Recording (the kinematic walk)
    bool demo_restart = true;
    int warm_left = 0, recorded = 0;
    uint64_t walks = 0;

    std::shared_ptr<GroundField> ground;    // the world's ground field when it has one; null on the flat ground prefab
    std::optional<WalkRoutes> routes;
    glm::vec3 origin = glm::vec3(0.f);      // this episode's spawn point
    bool route_lost = false;

    AnimalEpisode episode;                  // the one in progress
    std::vector<AnimalEpisode> episodes;    // finished ones (eval)

    int observationSize() const { return frame_size * animal_stack; }
    int actionSize() const { return animal_actions_per_joint * (int) body->def.joints.size(); }

    // Height of the hard ground under p
    float groundUnder(glm::vec3 p) const;
    // The trunk's origin over the ground under it
    float trunkHeight() const;
    // 0: standing; 1: the trunk under fall_height of its standing height; 2: a body that is neither a foot nor a
    // lower leg on the ground (`segment`)
    int fallen(int* segment = nullptr) const;
};

// The lowest point of a segment's shape, world
glm::vec3 lowestPoint(const SegmentDef& segment, glm::vec3 position, glm::quat rotation);

// The observation frame of the current state: header (the command in the trunk's heading frame, the speed
// difference, the height error, the soft depth), then forward axis, up axis and origin velocity of the trunk and of
// every joint's child, in the trunk's heading frame
void observeAnimal(const AnimalBot& bot, float* out);

void resetAnimalBot(entt::registry& reg);

class AnimalFactorySys : public System {
public:
    AnimalFactorySys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalFactory"; }
    void preinit() override;
    void init() override;
    void tick(double h) override {}
    void finish() override;
};

// Act phase: the window's action as motor gains and targets. A zero action holds the standing pose at half gains
class AnimalDriveSys : public System {
public:
    AnimalDriveSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalDrive"; }
    void tick(double h) override;
};

// After PhysicsStep: segment states, finite differences, the drawn shapes
class AnimalSyncSys : public System {
public:
    AnimalSyncSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalSync"; }
    void tick(double h) override;
};

// The command of a physical animal: the same process as the recorded walks follow
class AnimalTaskSys : public System {
public:
    AnimalTaskSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalTask"; }
    void tick(double h) override;
};

class AnimalTerminationSys : public System {
public:
    AnimalTerminationSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalTermination"; }
    void tick(double h) override;
};

class AnimalRewardsSys : public System {
public:
    AnimalRewardsSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalRewards"; }
    void tick(double h) override;
};

class AnimalObservationSys : public System {
public:
    AnimalObservationSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalObservation"; }
    void tick(double h) override;
};

class AnimalEpisodeSys : public System {
public:
    AnimalEpisodeSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalEpisode"; }
    void tick(double h) override;
};

// What the soft layer does to a body of `mass` moving at `velocity` in it: a drag in weights of the body per walk
// speed, full sideways, params.soft_lift of it upwards, none downwards; `inside`: how deep the body is, 0 at the
// surface to 1 at the hard ground; never more than what stops it within the step h
glm::vec3 softGroundForce(const AnimalParams& params, float mass, float walk_speed, float inside, glm::vec3 velocity, float h);

// Soft ground: bodies inside the layer over the hard ground are held, more sideways than upwards. Before PhysicsStep
class AnimalSoftGroundSys : public System {
public:
    AnimalSoftGroundSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalSoftGround"; }
    void tick(double h) override;
};

// The procedural walk played on the kinematic body, following its own command: the demonstrations. World context
// variable AnimalWalkState holds the walk. Before PhysicsStep
struct AnimalWalkState {
    GaitWalk walk;
    std::shared_ptr<GroundField> plane;     // when the world has no ground field
    bool ready = false;
    std::string problem;
    AnimalBodyBind bind;
};

class AnimalGaitPlayerSys : public System {
public:
    AnimalGaitPlayerSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalGaitPlayer"; }
    void init() override;
    void tick(double h) override;
};

// What a recording is checked for, per recorded tick or decision (world context variable, filled by AnimalChecks
// after AnimalSync)
struct AnimalDemoChecks {
    uint64_t ticks = 0, decisions = 0;
    float limit_excess = 0.f;               // radians: the furthest a joint was outside its limits
    int limit_joint = -1;
    uint64_t limit_ticks = 0;               // ticks with a joint more than 0.5 degree outside
    float axis_jump = 0.f;                  // the largest change of an observed axis between two decisions
    float velocity_jump = 0.f;              // ... of an observed velocity, in walk speeds
    uint64_t axis_jumps = 0;                // decisions where an observed axis changed by more than 0.6
    uint64_t heading_flips = 0;             // decisions where the trunk's heading turned by more than 60 degrees
    float lowest = 1e9f;                    // m: the lowest point of a body over the ground (negative: under it)
    uint64_t falls = 0;                     // ticks the fall test would have ended the episode
    int fall_cause = 0, fall_segment = -1;
    int axis_segment = -1, velocity_segment = -1, lowest_segment = -1;
    double progress = 0., commanded = 0.;   // m along the commanded heading as the reward measures it, and asked for
    float speed_error = 0.f;                // the worst |progress / commanded - 1| over a recorded walk
    float stretch = 0.f;                    // m: the furthest a segment's origin was from its joint of the skeleton
    float foot_slide = 0.f;                 // m/s: the fastest a planted foot's segment moved, 99 ticks in 100
    std::vector<float> slides;
    double walk_progress = 0., walk_commanded = 0.;
    std::vector<float> previous;            // the last recorded frame

    nlohmann::json json() const;
};

class AnimalChecksSys : public System {
public:
    AnimalChecksSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalChecks"; }
    void preinit() override;
    void tick(double h) override;
};

// The test of a body: on level ground, with a zero action (the drives hold the standing pose at half gains) it stands
// `seconds` at the 60 Hz step; limp (no gains), it falls and comes to rest
struct AnimalStandResult {
    bool stands = false;                    // not fallen, not sunk, not drifting, quiet at the end
    bool fell = false, rests = false;
    float trunk_drop = 0.f;                 // m the trunk ended lower than it started
    float trunk_height = 1.f;               // its height at the end, in standing heights
    float drift = 0.f;                      // m aside
    float tilt = 0.f;                       // degrees the trunk turned
    float max_speed = 0.f;                  // m/s of the fastest body over the last half
    float end_speed = 0.f;                  // ... over the last half second
    bool finite = true;
    int fall_cause = 0;
    std::string fall_segment;

    nlohmann::json json() const;
};
AnimalStandResult animalStandTest(const std::string& body_path, float seconds, bool limp);

void registerAnimalAgents();

}

#endif
