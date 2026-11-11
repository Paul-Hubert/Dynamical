#ifndef DY_RAGDOLL_H
#define DY_RAGDOLL_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

#include "humanbot_assets.h"

namespace dy {

class PhysicsWorld;

// The Jolt side of a BodyDef (docs/physical-agents/physics.md, Ragdoll build): one body per segment, one swing-twist
// constraint with position-and-velocity motors per joint, no collision between the segments. Shared by the human
// walker (HumanBot) and the animals (AnimalBot)
struct RagdollBuild {
    float total_mass = 70.f;
    float max_angular_velocity = 20.f;
    bool kinematic = false;
    bool cast = false;              // LinearCast and enhanced internal edge removal: on a mesh without thickness
};

struct RagdollParts {
    std::vector<JPH::BodyID> bodies;
    std::vector<JPH::Ref<JPH::SwingTwistConstraint>> constraints;   // empty when kinematic
    // Constraint space to body 1 (parent) and body 2 (child) rotation, X twist, Y plane, Z normal. Not Jolt's: its
    // Y is the normal and its Z the plane axis
    std::vector<glm::quat> frame1, frame2;
};

// Bodies at the rest pose of `def`, constraints made there
void buildRagdollParts(PhysicsWorld& world, const BodyDef& def, const RagdollBuild& build, RagdollParts& out);

// Removes the constraints and the bodies
void destroyRagdollParts(PhysicsWorld& world, RagdollParts& parts);

// Principal moments (at the body file's total mass, scaled) as a world-aligned inertia matrix
JPH::Mat44 ragdollInertia(glm::vec3 principal, glm::quat rotation, float scale);

}

#endif
