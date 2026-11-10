#ifndef DY_IK_H
#define DY_IK_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <entt/entt.hpp>

#include <string>
#include <vector>

#include "logic/systems/system.h"
#include "rig_pose.h"

namespace dy {

// Inverse kinematics (docs/physical-agents/ik-gait.md). Any system describes what it wants as IKTarget components;
// the Fabrik system solves every chain that has targets, in one batch, and writes joint angles into the rig's
// RigPose. Fabrik knows nothing of gaits or terrain

// FABRIK on positions: points[0] stays, points.back() reaches for the target, lengths[i] joins points[i] and
// points[i + 1]. With a pole (a direction), the inner points end on the pole's side of the root-tip line
struct FabrikResult {
    int iterations = 0;
    float error = 0.f;                          // distance left between the tip and the target
};
FabrikResult fabrik(std::vector<glm::vec3>& points, const std::vector<float>& lengths, glm::vec3 target,
    const glm::vec3* pole = nullptr, int max_iterations = 1024, float tolerance = 1e-5f);

// A chain of a rig: joints from the root to the tip, each one's child the next one's parent. Its points are the
// joints' anchors: the target position is for the last joint's anchor, the target rotation for the last joint's
// child (leg: hip, knee, ankle; the foot is the tip segment). One entity per chain
struct IKChain {
    entt::entity rig = entt::null;
    std::string name;
    std::vector<int> joints;
    glm::vec3 pole_rest = glm::vec3(0.f);       // where the inner joints point at the rest pose (zero: no pole)
    bool limits = true;                         // clamp to the rig model's joint limits

    // The last solve
    bool solved = false;
    int iterations = 0;
    float position_error = 0.f;                 // m between the tip anchor and the target
    float rotation_error = 0.f;                 // radians between the tip segment and the target
    float limit_excess = 0.f;                   // radians the limits took away, the worst joint
};

// What a system wants from a chain. Any number per chain: among the enabled ones, the highest priority wins and
// equal priorities blend by weight; a total weight under 1 blends the solution with the pose it found
struct IKTarget {
    entt::entity chain = entt::null;
    glm::vec3 position = glm::vec3(0.f);        // world, of the tip joint's anchor
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);     // world, of the tip segment
    bool use_rotation = true;
    glm::vec3 pole = glm::vec3(0.f);            // world direction; zero: the chain's rest pole carried by the root's parent
    float weight = 1.f;
    int priority = 0;
    bool enabled = true;
    std::string owner;                          // the system that updates it
};

struct IKGoal {
    glm::vec3 position = glm::vec3(0.f);
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    bool use_rotation = true;
    glm::vec3 pole = glm::vec3(0.f);
};

struct IKSolve {
    int iterations = 0;
    float position_error = 0.f, rotation_error = 0.f, limit_excess = 0.f;
};

// Solves one chain on a pose: the function behind the Fabrik system, also called by systems that need the solved
// pose within their own tick (GaitBalance)
IKSolve solveChain(const RigModel& model, RigPose& pose, const std::vector<int>& joints, glm::vec3 pole_rest,
    const IKGoal& goal, bool limits = true);

// Solves every chain with enabled targets and writes the joint angles into the rigs' poses
class FabrikSys : public System {
public:
    FabrikSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Fabrik"; }
    void tick(double h) override;
};

// Forward kinematics of every RigPose onto the kinematic bodies of the world's HumanBot (MoveKinematic)
class RigPoseApplySys : public System {
public:
    RigPoseApplySys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "RigPoseApply"; }
    void tick(double h) override;
};

void registerIK();

}

#endif
