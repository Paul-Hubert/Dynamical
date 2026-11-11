#include "ragdoll.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <algorithm>

#include "physics/physics_sys.h"
#include "physics/physics_world.h"

using namespace dy;

namespace {

JPH::Ref<JPH::Shape> makeShape(const ShapeDef& s) {
    JPH::Ref<JPH::Shape> shape;
    switch(s.type) {
    case ShapeDef::Type::Capsule: shape = new JPH::CapsuleShape(s.half_height, s.radius); break;
    case ShapeDef::Type::Sphere: shape = new JPH::SphereShape(s.radius); break;
    case ShapeDef::Type::Box: shape = new JPH::BoxShape(toJolt(s.half_extents), s.convex_radius >= 0.f ? s.convex_radius : std::min({ 0.05f, s.half_extents.x, s.half_extents.y, s.half_extents.z })); break;
    }
    return shape;
}

// The segment's shape in its body frame, its centre of mass where the body file says
JPH::Ref<JPH::Shape> segmentShape(const SegmentDef& seg) {

    JPH::Ref<JPH::Shape> inner;
    if(seg.shapes.size() == 1) {
        const ShapeDef& s = seg.shapes[0];
        inner = new JPH::RotatedTranslatedShape(toJolt(s.center), toJolt(s.rotation), makeShape(s));
    } else {
        JPH::StaticCompoundShapeSettings compound;
        for(const ShapeDef& s : seg.shapes) compound.AddShape(toJolt(s.center), toJolt(s.rotation), makeShape(s));
        auto result = compound.Create();
        if(result.HasError()) throw AssetError("segment " + seg.name + ": " + result.GetError().c_str());
        inner = result.Get();
    }
    JPH::Vec3 offset = toJolt(seg.center_of_mass) - inner->GetCenterOfMass();
    return new JPH::OffsetCenterOfMassShape(inner, offset);

}

}

JPH::Mat44 dy::ragdollInertia(glm::vec3 principal, glm::quat rotation, float scale) {
    JPH::Mat44 r = JPH::Mat44::sRotation(toJolt(rotation));
    JPH::Mat44 d = JPH::Mat44::sScale(toJolt(principal * scale));
    d.SetColumn4(3, JPH::Vec4(0, 0, 0, 1));
    JPH::Mat44 m = r * d * r.Transposed3x3();
    m.SetColumn4(3, JPH::Vec4(0, 0, 0, 1));
    return m;
}

void dy::buildRagdollParts(PhysicsWorld& world, const BodyDef& def, const RagdollBuild& build, RagdollParts& out) {

    auto& bodies = world.bodies();
    const float mass_scale = build.total_mass / def.total_mass;

    JPH::Ref<JPH::GroupFilterTable> filter = new JPH::GroupFilterTable((JPH::uint) def.segments.size());
    for(JPH::uint i = 0; i < def.segments.size(); i++) {
        for(JPH::uint j = i + 1; j < def.segments.size(); j++) filter->DisableCollision(i, j);
    }

    out.bodies.resize(def.segments.size());
    for(size_t i = 0; i < def.segments.size(); i++) {
        const SegmentDef& seg = def.segments[i];
        JPH::BodyCreationSettings settings(segmentShape(seg), JPH::RVec3(toJolt(seg.rest_position)), toJolt(seg.rest_rotation),
            build.kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic, build.animal_layer ? PhysicsLayers::animal_layer : PhysicsLayers::agent_layer);
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        settings.mMassPropertiesOverride.mMass = build.total_mass * seg.percentage / 100.f;
        settings.mMassPropertiesOverride.mInertia = ragdollInertia(seg.inertia, seg.inertia_rotation, mass_scale);
        settings.mFriction = def.friction;
        settings.mRestitution = def.restitution;
        settings.mLinearDamping = def.linear_damping;
        settings.mAngularDamping = def.angular_damping;
        settings.mMaxAngularVelocity = build.max_angular_velocity;
        settings.mMotionQuality = JPH::EMotionQuality::Discrete;
        settings.mAllowSleeping = false;
        settings.mCollideKinematicVsNonDynamic = build.kinematic;
        // The marching cubes mesh has no thickness and edges: a fast limb is cast along its motion instead of
        // stepping through the surface (with discrete steps a foot of the flat policy ended 0.5 m under it), and a
        // sole that crosses an edge between two triangles does not catch on it. The flat ground keeps discrete steps
        if(!build.kinematic && build.cast) {
            settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
            settings.mEnhancedInternalEdgeRemoval = true;
        }
        settings.mCollisionGroup = JPH::CollisionGroup(filter, build.group, (JPH::CollisionGroup::SubGroupID) i);
        // Whose segment this is, for a contact listener
        settings.mUserData = ((uint64_t) build.group << 32) | (uint64_t) i;
        out.bodies[i] = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
    }

    out.constraints.clear();
    out.frame1.resize(def.joints.size());
    out.frame2.resize(def.joints.size());

    for(size_t j = 0; j < def.joints.size(); j++) {
        const JointDef& joint = def.joints[j];
        const SegmentDef& parent = def.segments[joint.parent];
        const SegmentDef& child = def.segments[joint.child];

        // Constraint frame at the rest pose: X twist, Y plane, Z normal
        glm::vec3 x = glm::normalize(joint.twist_axis), y = glm::normalize(joint.plane_axis - x * glm::dot(joint.plane_axis, x));
        glm::vec3 z = glm::cross(x, y);
        glm::quat constraint = glm::quat_cast(glm::mat3(x, y, z));
        out.frame1[j] = glm::normalize(glm::inverse(parent.rest_rotation) * constraint);
        out.frame2[j] = glm::normalize(glm::inverse(child.rest_rotation) * constraint);
        if(build.kinematic) continue;

        JPH::SwingTwistConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPosition1 = settings.mPosition2 = JPH::RVec3(toJolt(joint.anchor));
        settings.mTwistAxis1 = settings.mTwistAxis2 = toJolt(x);
        settings.mPlaneAxis1 = settings.mPlaneAxis2 = toJolt(y);
        settings.mSwingType = JPH::ESwingType::Cone;
        settings.mPlaneHalfConeAngle = joint.swing_y;
        settings.mNormalHalfConeAngle = joint.swing_z;
        settings.mTwistMinAngle = joint.twist_min;
        settings.mTwistMaxAngle = joint.twist_max;
        // Constraints are made at the rest pose: bodies are still there
        JPH::Ref<JPH::Constraint> c = bodies.CreateConstraint(&settings, out.bodies[joint.parent], out.bodies[joint.child]);
        auto* swing = static_cast<JPH::SwingTwistConstraint*>(c.GetPtr());
        for(auto* motor : { &swing->GetSwingMotorSettings(), &swing->GetTwistMotorSettings() }) {
            motor->mSpringSettings.mMode = JPH::ESpringMode::StiffnessAndDamping;
            motor->mSpringSettings.mStiffness = 0.f;
            motor->mSpringSettings.mDamping = 0.f;
        }
        swing->SetSwingMotorState(JPH::EMotorState::PositionAndVelocity);
        swing->SetTwistMotorState(JPH::EMotorState::PositionAndVelocity);
        world.system().AddConstraint(c);
        out.constraints.emplace_back(swing);
    }

}

void dy::destroyRagdollParts(PhysicsWorld& world, RagdollParts& parts) {
    for(auto& c : parts.constraints) world.system().RemoveConstraint(c);
    parts.constraints.clear();
    for(auto id : parts.bodies) {
        world.bodies().RemoveBody(id);
        world.bodies().DestroyBody(id);
    }
    parts.bodies.clear();
}
