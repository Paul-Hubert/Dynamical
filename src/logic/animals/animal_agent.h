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

// The order next to the walk command (docs/action-agents.md): what the animal is doing and where its head is asked to
// point. A second sensor of the walker, "nogail_order": the head's yaw and pitch offsets over their limits and the mode
// as a one-hot. GAIL's discriminator does not see it (the fork hides sensors named nogail), so the demonstrations of
// the procedural walk stay what they are
enum class AnimalMode : int { Move, Feed, Attack, Mate, Rest };
constexpr int animal_modes = 5;
constexpr int animal_order_size = 2 + animal_modes;
constexpr float animal_head_yaw = 1.f;          // radians to either side
constexpr float animal_head_down = 1.1f;        // radians down
constexpr float animal_head_up = 0.5f;
const char* animalModeName(int mode);

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
    float over_speed = 0.f;                 // what going faster than the command costs, against what going slower loses
                                            // (0: nothing, the walker's reward; 1: as much, and a command to stand is one
                                            // to stand still)
    // With orders, per decision: the head where it is asked to point, and what the mode asks: the muzzle down (feed),
    // the head thrown forward (attack), standing still (mate), everything still (rest). As much as the command's
    // reward: at 0.1 and 0.15 the first walkers kept walking through every order (GAIL and the command paid more)
    float head_reward = 0.3f;
    float mode_reward = 0.4f;
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
    bool orders = false;                    // modes and head offsets are drawn next to the commands, the order sensor is on
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
    // The same per kind of command (commandKind: 0 stands, 1 walks, 2 runs)
    float kind_seconds[3] = { 0.f, 0.f, 0.f }, kind_distance[3] = { 0.f, 0.f, 0.f };
    float kind_progress[3] = { 0.f, 0.f, 0.f }, kind_commanded[3] = { 0.f, 0.f, 0.f };
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

    // The order (AnimalMode, head offsets), and the one the last observation carried
    bool orders = false;
    int mode = 0;
    glm::vec2 head = glm::vec2(0.f);        // yaw to the left and pitch up asked of the head, radians from where it points standing
    float order_until = 0.f;                // seconds of the episode at which the next order is drawn (the single animal's task)
    int snapshot_mode = 0;
    glm::vec2 snapshot_head = glm::vec2(0.f);
    int head_segment = 0;
    glm::vec3 muzzle = glm::vec3(0.f);      // the front of the head, in the head segment's frame
    glm::vec3 head_stand = glm::vec3(1.f, 0.f, 0.f);    // from the head's origin to the muzzle, in the trunk's heading frame, standing
    float muzzle_stand = 1.f;               // m: how high the muzzle is when the animal stands
    std::vector<char> neck;                 // per segment: the head and what carries it up to the shoulders. With orders
                                            // (neck_may_touch) the ground is theirs too: an animal that feeds puts its head down
    bool neck_may_touch = false;

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

    // The muzzle in the world, and where the head points (from its origin to the muzzle)
    glm::vec3 muzzlePoint() const { return position[head_segment] + rotation[head_segment] * muzzle; }
    glm::vec3 headDirection() const;
    // Where the head would point with the offsets `asked` (yaw, pitch), for the trunk as it is
    glm::vec3 askedHeadDirection(glm::vec2 asked) const;
};

// What a decision window is worth to the walker: along the command, and with orders the head and the mode
struct AnimalRating {
    glm::vec3 velocity = glm::vec3(0.f);    // of the trunk over the window
    float along = 0.f;                      // m/s along the commanded heading
    float root = 0.f;                       // the command's reward
    float heading_error = 0.f;              // radians between the trunk's heading and the command
    float head = 0.f, mode = 0.f;           // the order's rewards
    float head_error = 0.f;                 // radians between where the head points and where it is asked to
    float muzzle_height = 0.f;              // m over the ground
};
AnimalRating rateAnimal(const AnimalBot& bot, double window);

// The order of a bot as its sensor holds it (animal_order_size values)
void observeOrder(const AnimalBot& bot, float* out);

// A head offset inside its limits
glm::vec2 clampHead(glm::vec2 head);

// The lowest point of a segment's shape, world
glm::vec3 lowestPoint(const SegmentDef& segment, glm::vec3 position, glm::quat rotation);

// The observation frame of the current state: header (the command in the trunk's heading frame, the speed
// difference, the height error, the soft depth), then forward axis, up axis and origin velocity of the trunk and of
// every joint's child, in the trunk's heading frame
void observeAnimal(const AnimalBot& bot, float* out);

void resetAnimalBot(entt::registry& reg);

// What the systems below do for the world's animal, for any AnimalBot (the sandbox holds many, as components:
// docs/action-agents.md)

// The body, the params and the ranges a spawn names
void loadAnimalAssets(AnimalBot& bot, const AnimalSpawn& spawn);

struct AnimalBuild {
    uint32_t group = 0;                     // the ragdoll's collision group: one per animal of a world
    bool collide = false;                   // with the other animals (PhysicsLayers::animal_layer)
};
// The ragdoll of bot.body in the world, at its rest pose, and the bot's arrays
void buildAnimalRagdoll(entt::registry& reg, AnimalBot& bot, const AnimalBuild& options = {});
void destroyAnimalRagdoll(entt::registry& reg, AnimalBot& bot);

// The bodies in the standing pose, the trunk over `origin`, heading `yaw`, at rest; on_field: raised onto bot.ground
void standAnimalBot(entt::registry& reg, AnimalBot& bot, glm::vec3 origin, float yaw, bool on_field);

// A window's action (animal_actions_per_joint values a joint) as motor gains and targets; strength: of the drives
// (1: the body file's)
void driveAnimalBot(AnimalBot& bot, const float* action, float strength = 1.f);

void readAnimalState(entt::registry& reg, AnimalBot& bot);
// bot.params: the base params with the world's environment parameters over them
void applyAnimalEnvParams(entt::registry& reg, AnimalBot& bot);
// After a physics step: the segments' states and the finite differences of their origins
void syncAnimalBot(entt::registry& reg, AnimalBot& bot, double h);

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
    // With ranges that run (GaitRanges::run) a recorded walk is a run from end to end or has none: the two gaits are
    // not blended. With ranges that stand (idle), a walk goes to the standing pose as its command comes to a halt
    GaitWalk run;
    bool has_run = false;
    bool running = false;                   // this walk is a run
    std::vector<glm::mat4> blended;
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
    uint64_t kind_decisions[3] = { 0, 0, 0 };   // recorded decisions whose command stands, walks, runs
    double idle_distance = 0.;              // m the trunk covered while its command stood
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
