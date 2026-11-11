#ifndef DY_HUMANBOT_ASSETS_H
#define DY_HUMANBOT_ASSETS_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace dy {

// Files exported from Unity (docs/physical-agents/assets.md), converted once at load to the engine frame: right
// handed, y up, -z forward. Mirroring z: points (x, y, -z), quaternions (-x, -y, z, w)
inline glm::vec3 unityToEngine(glm::vec3 v) { return glm::vec3(v.x, v.y, -v.z); }
inline glm::quat unityToEngine(glm::quat q) { return glm::quat(q.w, -q.x, -q.y, q.z); }

struct AssetError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ShapeDef {
    enum class Type : uint8_t { Capsule, Box, Sphere };
    Type type = Type::Capsule;
    std::string source;
    glm::vec3 center = glm::vec3(0.f);              // in the segment's frame
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);  // in the segment's frame; a capsule's axis is its local Y
    float radius = 0.f, half_height = 0.f;          // capsule: of the cylinder part; sphere: radius
    glm::vec3 half_extents = glm::vec3(0.f);        // box
    float convex_radius = -1.f;                     // box: how round its edges are; < 0: the least of 5 cm and its half extents (the human walker)
};

struct SegmentDef {
    std::string name;
    int parent = -1;
    std::string role;                               // pelvis, spine, head, foot or empty
    glm::vec3 rest_position = glm::vec3(0.f);       // bone origin: Unity's rest pose, the constraints' reference
    glm::quat rest_rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 spawn_position = glm::vec3(0.f);      // where episodes start: the rest pose, knee bent inside its stop
    glm::quat spawn_rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    float percentage = 0.f;                         // of the total mass
    glm::vec3 center_of_mass = glm::vec3(0.f);      // in the segment's frame
    glm::vec3 inertia = glm::vec3(0.f);             // principal moments at the unity total mass
    glm::quat inertia_rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    std::vector<ShapeDef> shapes;
};

struct JointDef {
    std::string name;
    int parent = -1, child = -1;                    // segments
    char side = 'C';
    glm::vec3 anchor = glm::vec3(0.f);              // world, at the rest pose
    glm::vec3 twist_axis = glm::vec3(1.f, 0.f, 0.f);    // world, at the rest pose
    glm::vec3 plane_axis = glm::vec3(0.f, 1.f, 0.f);
    glm::vec3 unity_axis = glm::vec3(1.f, 0.f, 0.f);    // Unity's joint axes as vectors in the child's frame (engine frame)
    glm::vec3 unity_secondary = glm::vec3(0.f, 1.f, 0.f);
    float twist_min = 0.f, twist_max = 0.f;         // radians about the twist axis: Unity limit values (its joint angle is opposite the rotation about the axis; the mirror cancels that)
    float swing_y = 0.f, swing_z = 0.f;             // half angles about the plane and the normal axis
    float spring_factor = 1.f, damper_factor = 1.f, torque_factor = 1.f;
};

// The rigged body of one version (heidelbot_notoes, heidelbot_notoes_h173). Immutable once loaded
struct BodyDef {
    std::string name;
    std::string file_hash;                          // SHA-256 of the file: run metadata, demo cache
    float total_mass = 70.f;
    float height = 1.89f;
    float unity_est0 = 0.f;                         // Unity's head-foot height at spawn (oracle only)
    float unity_lowest_y = 0.f;
    float linear_damping = 0.f, angular_damping = 0.f;
    float friction = 1.f, restitution = 0.f;
    std::vector<SegmentDef> segments;
    std::vector<JointDef> joints;                   // the trainer-facing order

    int pelvis = -1, spine = -1, head = -1, right_foot = -1, left_foot = -1;
    int spine_joint = -1;

    int segmentIndex(const std::string& name) const;
    // The segment of joint j's parent frame in the observations: its parent, or the Spine when that is the pelvis
    int frameSegment(int joint) const;
    // Unity's est0 as this file defines it: the head-foot height at the spawn pose, on bone origins
    float est0() const;
};

std::shared_ptr<const BodyDef> loadBody(const std::string& path);
std::shared_ptr<const BodyDef> parseBody(const nlohmann::json& json, const std::string& hash = {});

// Per-tick world poses of every segment (engine frame), baked from a retargeted Unity clip
struct PoseTrack {
    std::string body, controller;
    float dt = 1.f / 60.f;
    float state_speed = 1.f;
    int frames = 0, segments = 0;
    std::string file_hash;
    std::vector<float> data;                        // frame-major: pos.xyz, rot.xyzw per segment

    glm::vec3 position(int frame, int segment) const;
    glm::quat rotation(int frame, int segment) const;
};

std::shared_ptr<const PoseTrack> loadTrack(const std::string& json_path);

// Resolved prefab and scene values of one experiment line, with the env params applied on top each episode
struct AgentParams {
    std::string behavior = "HeidelBotBehavior";
    std::string body;
    std::string file_hash;

    int max_frames_60hz = 2000, arc_frames_60hz = 400;
    float total_mass = 70.f;
    float rotation_spring = 2983.68f, rotation_damper = 336.f, max_torque = 200000.f, torque_factor = 1.f;
    float max_angular_velocity = 20.f, max_target_angular_velocity = 1.0472f;
    float drive_gain_scale = 1.f;

    float target_speed = 1.36f;
    std::vector<float> speeds;
    bool continuous_speed = false;
    float target_speed_min = 0.f, target_speed_max = -1.f;
    bool randomize = false, random_target = false, random_speed = false, random_init = false;
    float random_init_chance = 0.2f;
    bool pushing = false, pulling = false;
    float push_chance = 0.001f, push_force = 5.f, pull_chance = 0.001f, pull_force = 1.f;

    float root_reward = 0.34f, head_spring_reward = 0.f, head_damper_reward = 0.f, life_reward = 0.f, dying_reward = 0.f;
    float win_reward = 0.f, death_penalty = 0.f, reward_factor = 1.f, collision_penalty = 0.f;
    float dying_height_factor = 0.7f;
    int max_dying_frames = 0;
    bool die_on_fall = true, reset_on_alive = true;
    bool speed_diff_zero_recording = true;

    // Energy provider (EnergyRewardProvider)
    float energy_weight = 0.f, energy_scale = 30000.f, energy_speed_edge = 0.15f, energy_min_speed_floor = 0.1f, energy_tau = 0.2f;
    float energy_positive_multiplier = 1.f, energy_negative_multiplier = 1.f, stats_settle_seconds = 2.f;
    float torque_rate_weight = 0.f;             // J per N m of motor torque change, summed over joints and ticks
};

AgentParams parseParams(const nlohmann::json& json);
std::shared_ptr<const AgentParams> loadParams(const std::string& path);

// SHA-256 of a file's bytes, lower-case hex
std::string fileSha256(const std::string& path);

}

#endif
