#ifndef DY_RIG_POSE_H
#define DY_RIG_POSE_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <entt/entt.hpp>

#include <memory>
#include <vector>

#include "humanbot_assets.h"

namespace dy {

// A rig's pose as joint angles (docs/physical-agents/ik-gait.md): the pelvis segment's world pose and one rotation
// per joint, in the joint's constraint frame (X twist, Y plane, Z normal), identity at the rest pose. Component of
// the rig entity (the one with HumanBotSpawn): GaitBase writes it, Fabrik corrects it, RigPoseApply moves the bodies
struct RigPose {
    glm::vec3 root_position = glm::vec3(0.f);
    glm::quat root_rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    std::vector<glm::quat> joint;
    bool teleport = true;                       // the next RigPoseApply sets the bodies instead of moving them
};

// World pose of every segment
struct SegmentPoses {
    std::vector<glm::vec3> position;
    std::vector<glm::quat> rotation;
};

// A joint's limits: twist in [twist_min, twist_max] and the swing inside an elliptical cone, as the Jolt constraint
struct JointLimit {
    float twist_min = 0.f, twist_max = 0.f;
    float swing_y = 0.f, swing_z = 0.f;
};

// Twist then swing of a constraint-frame rotation: q = swing * twist (Jolt's decomposition)
struct SwingTwist {
    float twist = 0.f;                          // radians about X
    glm::quat swing = glm::quat(1.f, 0.f, 0.f, 0.f);    // x = 0
};
SwingTwist swingTwist(glm::quat q);
glm::quat fromSwingTwist(const SwingTwist& st);

// The kinematic tree of a body file, without Jolt: forward kinematics, joint conversions, limits, centre of mass
class RigModel {
public:
    explicit RigModel(std::shared_ptr<const BodyDef> body);

    std::shared_ptr<const BodyDef> body;
    int segments = 0, joints = 0;
    std::vector<int> order;                     // joints, parents first
    std::vector<int> joint_of_segment;          // the joint whose child is the segment, -1 for the pelvis
    std::vector<glm::quat> frame1, frame2;      // constraint frame in the parent's and in the child's frame
    std::vector<glm::quat> rest_frame;          // constraint frame in the world at the rest pose
    std::vector<glm::vec3> anchor_parent, anchor_child;     // the anchor in the parent's and in the child's frame
    std::vector<JointLimit> limits;             // what the IK clamps to: the body file's, or enlarged (enlarge)
    std::vector<JointLimit> body_limits;        // the body file's
    std::vector<float> mass;                    // per segment, fractions of the total

    int jointIndex(const std::string& name) const;

    RigPose restPose() const;
    RigPose spawnPose() const;                  // the body file's spawn pose (knees inside their stop)

    void forward(const RigPose& pose, SegmentPoses& out) const;
    // World position of joint j's anchor
    glm::vec3 anchor(const SegmentPoses& poses, int j) const {
        const JointDef& joint = body->joints[j];
        return poses.position[joint.parent] + poses.rotation[joint.parent] * anchor_parent[j];
    }

    // The joint rotation that puts the child at child_world under a parent at parent_world
    glm::quat jointRotation(int j, glm::quat parent_world, glm::quat child_world) const {
        return glm::normalize(glm::inverse(parent_world * frame1[j]) * (child_world * frame2[j]));
    }
    glm::quat childRotation(int j, glm::quat parent_world, glm::quat q) const {
        return glm::normalize(parent_world * frame1[j] * q * glm::inverse(frame2[j]));
    }
    // A rotation of the child relative to its parent written in the rest pose's world axes (x right, y up, -z forward)
    glm::quat fromRestDelta(int j, glm::quat delta) const {
        return glm::normalize(glm::inverse(rest_frame[j]) * delta * rest_frame[j]);
    }

    // q inside joint j's limits; excess: the angle taken away, radians
    glm::quat clamp(int j, glm::quat q, float* excess = nullptr) const { return clampTo(limits[j], q, excess); }
    static glm::quat clampTo(const JointLimit& limit, glm::quat q, float* excess = nullptr);
    // How far q is outside a limit, radians (0 inside)
    static float excess(const JointLimit& limit, glm::quat q);

    // Widens joint j's IK limits (degrees added on each side); the body file's stay in body_limits
    void enlarge(int j, float twist_low_deg, float twist_high_deg, float swing_y_deg, float swing_z_deg);

    glm::vec3 centerOfMass(const SegmentPoses& poses) const;

};

// The rig of a world: component of the entity with HumanBotSpawn, next to its RigPose
struct Rig {
    std::shared_ptr<RigModel> model;
};

// The rig entity with Rig and RigPose (at the spawn pose), created on the first call. Needs the world's HumanBot
// (AgentFactory before in the fixed list); entt::null without one
entt::entity ensureRig(entt::registry& reg);

}

#endif
