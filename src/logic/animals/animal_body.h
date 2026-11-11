#ifndef DY_ANIMAL_BODY_H
#define DY_ANIMAL_BODY_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "logic/agents/humanbot_assets.h"
#include "logic/animals/animal.h"
#include "logic/animals/gait.h"

namespace dy {

// The physical body of an animal of the library (docs/physical-agents/reports/animal-agents.md): which joints of its
// skeleton are bodies, their shapes and masses, the joints between them with their limits and drives. Generated from
// the library and the gait analysis by generateAnimalBody, with no knowledge of the species; written next to the
// library as bodies/<id>.body.json, engine frame (metres, Y up, the animal looks down -Z)

// What the generator can be told: resources/agents/animal_overrides.json ("defaults", then "species"/<id>)
struct AnimalBodyOptions {
    float density = 1000.f;             // kg/m3 of the mesh's volume
    float mass = 0.f;                   // kg; 0: from the volume
    int max_bodies = 24;
    int max_leg_bodies = 4;             // per leg, the foot included
    float merge_loss = 3e-4f;           // a joint that moves less than this (mass part x lever in sizes x radians) rides on its parent
    float limit_margin = 5.f;           // degrees added to each joint limit ...
    float limit_margin_part = 0.1f;     // ... plus this part of the range
    float stiffness = 4.3f;             // drive stiffness of the joints that carry the animal, in M g H per radian (the man's 2983.68 N m/rad)
    float appendage_stiffness = 30.f;   // ... of a neck, a head, a tail, an arm: in weight x lever of what they carry
    float damping_time = 0.1126f;       // damping / stiffness in seconds at the man's size (336 / 2983.68), scaled by sqrt(H)
    float torque_angle = 0.5f;          // the torque limit: the full spring this many radians from its target
    float friction = 1.f;
    float min_leg_mass = 0.012f;        // part of the total a leg segment weighs at least, 
    float min_mass = 0.004f;            // ... and any other
    float min_inertia = 0.15f;          // a segment's least principal moment, as a part of its greatest
    int velocity_steps = 10, position_steps = 2;    // of the solver, for an animal on four legs
    int biped_velocity_steps = 40, biped_position_steps = 8;    // ... on two
    float inertia_condition = 30.f;     // > 0: (damping x dt + stiffness x dt2) of a drive over the least moment of inertia of its bodies, at most
    int min_substeps = 1;               // physics steps per 60 Hz tick at least (more for small animals, by their size)
    std::string stand;                  // clip of the standing pose; empty: the idle clip, else the bind pose

    static AnimalBodyOptions parse(const nlohmann::json& overrides, const std::string& species);
    nlohmann::json json() const;
};

// One body file: nlohmann::json of the format below, or an error. `walk` is the gait model of the animal's walk clip
nlohmann::json generateAnimalBody(const AnimalLibrary::Entry& entry, const AnimalSpecies& species, const std::vector<AnimalVertex>& vertices,
    const std::vector<uint32_t>& indices, const GaitModel& walk, const AnimalBodyOptions& options, std::string& error);

struct AnimalJointDrive {
    float stiffness = 0.f;              // N m/rad at full gain
    float damping = 0.f;                // N m s/rad at full gain
    float max_torque = 0.f;             // N m
    glm::vec3 stand = glm::vec3(0.f);   // the standing pose in constraint space: twist, swing about y, swing about z (radians)
    bool hinge = false;
    bool support = false;               // between a foot and the trunk
};

struct AnimalLegDef {
    std::vector<int> segments;          // the hip first, the foot last
    bool front = false, left = false;
};

// A loaded body file. def holds what the shared ragdoll build needs (segments, shapes, joints: rest pose = every
// joint in the middle of its cone, spawn pose = the animal standing)
struct AnimalBody {
    std::string species;
    std::string behavior;               // one per species: sizes differ
    std::string stand_clip;             // the clip of the standing pose ("name" or "name @ seconds"), empty: the bind pose
    std::string file_hash;
    BodyDef def;
    std::vector<int> segment_joint;     // per segment: the joint of the skeleton it follows
    std::vector<int> joint_segment;     // per joint of the skeleton: the segment it rides on
    std::vector<glm::vec3> offset;      // per segment: its origin in its parent's frame (the root: zero)
    std::vector<char> may_touch;        // per segment: a foot or a lower leg, the ground is theirs
    std::vector<char> is_foot;
    std::vector<int> segment_parent_joint;  // per segment: the joint (of def.joints) that holds it, -1 for the root
    std::vector<AnimalJointDrive> drives;   // per joint
    std::vector<AnimalLegDef> legs;
    int root = 0;                       // the trunk: the segment every other hangs from
    float mass = 0.f, height = 1.f, length = 1.f;
    float com_height = 1.f;             // of the centre of mass over the ground, standing
    float stand_height = 1.f;           // of the trunk's origin over the ground, standing
    float walk_speed = 1.f;             // m/s of the walk clip
    float max_angular_velocity = 20.f, max_target_angular_velocity = 1.0472f;
    int substeps = 1;

    // Per joint j: constraint space (Jolt's: X twist, Y normal, Z plane) in the parent's and in the child's frame
    std::vector<glm::quat> frame1, frame2;
};

std::shared_ptr<const AnimalBody> parseAnimalBody(const nlohmann::json& json, const std::string& hash = {});
std::shared_ptr<const AnimalBody> loadAnimalBody(const std::string& path);

// resources/animals/bodies/<id>.body.json of the library in use
std::filesystem::path animalBodyFile(const std::string& species);
// The path as given when it exists, else the library's bodies folder, else the executable's resources
std::string animalBodyPath(const std::string& given);

// The joint's rotation in constraint space as Jolt measures it, from the world rotations of its two bodies:
// twist (radians about X), and the y and z components of the swing quaternion
struct JointAngles {
    float twist = 0.f;
    float swing_y = 0.f, swing_z = 0.f;         // components of the swing quaternion: sin(half angle) about Y and about Z
};
JointAngles jointAngles(const AnimalBody& body, int joint, glm::quat parent, glm::quat child);
// How far outside its limits the joint is, radians (0 inside): the twist beyond its range, the swing beyond its cone
float limitExcess(const AnimalBody& body, int joint, const JointAngles& angles);
// The constraint space rotation of a twist and a swing given as angles (radians about X, about Y, about Z)
glm::quat constraintRotation(float twist, float swing_y, float swing_z);

// Rotation of a matrix with scale (and possibly mirrored)
glm::quat matrixRotation(const glm::mat4& m);

// The skeleton's bind pose as body frames: per joint, the rotation and the origin (model space) its segment has there
struct AnimalBodyBind {
    std::vector<glm::quat> rotation_inverse;    // per joint of the skeleton: inverse of its bind rotation
    std::vector<glm::mat4> bind;                // per joint: its bind matrix
    std::vector<glm::mat4> stand;               // per joint: the standing pose the body file was made from, model space
};
AnimalBodyBind animalBodyBind(const AnimalBody& body, const AnimalSpecies& species);

// Segment poses for the joints of a pose (model space matrices), carried to the world by `to_world` (no scale): each
// segment where its joint is. Where the pose stretches a bone (the procedural walk carries the joints of the trunk
// to the curve of the path) the anchors of the ragdoll would not hold: a planted foot stays planted instead, and
// what an observation holds, axes and velocities, is that of the walk
void segmentPoses(const AnimalBody& body, const AnimalBodyBind& bind, const std::vector<glm::mat4>& joints, const glm::mat4& to_world,
    std::vector<glm::vec3>& positions, std::vector<glm::quat>& rotations);

// The reverse, to draw the skinned mesh on a ragdoll: joints that are bodies take their body's pose, the others ride
// on the segment they belong to as they are in the standing pose (wings folded, not spread as in the bind pose).
// `world`: the joints' matrices in the space the segments are given in
void skeletonFromSegments(const AnimalBody& body, const AnimalBodyBind& bind, const AnimalSpecies& species,
    const std::vector<glm::vec3>& positions, const std::vector<glm::quat>& rotations, std::vector<glm::mat4>& world);

}

#endif
