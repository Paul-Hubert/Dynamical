#include "player_systems.h"

#include <Jolt/Physics/Body/BodyLock.h>

#include "logic/components/rigid_body.h"
#include "physics/physics_sys.h"

#include <algorithm>
#include <cmath>

using namespace dy;

// Shared helpers

JPH::BodyID dy::playerBodyId(const entt::registry& reg, entt::entity entity) {
    if(entity == entt::null || !reg.valid(entity)) return JPH::BodyID();
    auto* body = reg.try_get<RigidBody>(entity);
    if(!body || !body->created()) return JPH::BodyID();
    return JPH::BodyID(body->body);
}

entt::entity dy::bodyEntity(const entt::registry& reg, PhysicsWorld& world, JPH::BodyID id) {
    if(id.IsInvalid()) return entt::null;
    JPH::uint64 data = world.bodies().GetUserData(id);
    if(data == 0) return entt::null;
    entt::entity entity = (entt::entity) (data - 1);
    if(!reg.valid(entity)) return entt::null;
    // A recycled id is not this body's entity
    auto* body = reg.try_get<RigidBody>(entity);
    return body && body->body == id.GetIndexAndSequenceNumber() ? entity : entt::null;
}

float dy::bodyMass(PhysicsWorld& world, JPH::BodyID id) {
    JPH::BodyLockRead lock(world.system().GetBodyLockInterfaceNoLock(), id);
    if(!lock.Succeeded() || !lock.GetBody().IsDynamic()) return 0.f;
    float inverse = lock.GetBody().GetMotionProperties()->GetInverseMass();
    return inverse > 0.f ? 1.f / inverse : 0.f;
}

PlayerCollision& dy::playerCollision(entt::registry& reg) {
    if(auto* c = reg.ctx().find<PlayerCollision>()) return *c;
    auto& c = reg.ctx().emplace<PlayerCollision>();
    using namespace PlayerGroups;
    c.filter = new JPH::GroupFilterTable(count);
    // Unity: the joint between a hand and the player has no collision; a hand and what it holds ignore each other;
    // so do the FPS player and what it holds
    c.filter->DisableCollision(body, left_hand);
    c.filter->DisableCollision(body, right_hand);
    c.filter->DisableCollision(left_hand, held_left);
    c.filter->DisableCollision(right_hand, held_right);
    c.filter->DisableCollision(left_hand, held_both);
    c.filter->DisableCollision(right_hand, held_both);
    c.filter->DisableCollision(body, held_body);
    // A rider and the animal under it, its hands too
    c.filter->DisableCollision(body, mount);
    c.filter->DisableCollision(left_hand, mount);
    c.filter->DisableCollision(right_hand, mount);
    return c;
}

void dy::setPlayerGroup(entt::registry& reg, PhysicsWorld& world, JPH::BodyID id, uint32_t group, PlayerGroups::Sub sub) {
    JPH::BodyLockWrite lock(world.system().GetBodyLockInterfaceNoLock(), id);
    if(!lock.Succeeded()) return;
    lock.GetBody().SetCollisionGroup(JPH::CollisionGroup(playerCollision(reg).filter, group, (JPH::CollisionGroup::SubGroupID) sub));
}

void dy::clearPlayerGroup(PhysicsWorld& world, JPH::BodyID id) {
    JPH::BodyLockWrite lock(world.system().GetBodyLockInterfaceNoLock(), id);
    if(!lock.Succeeded()) return;
    lock.GetBody().SetCollisionGroup(JPH::CollisionGroup());
}



// BodyDrives

namespace {

// Which way Jolt's motors count a target velocity (tests PLAYER.4)
constexpr float linear_sign = 1.f;
constexpr float angular_sign = 1.f;

const JPH::SixDOFConstraintSettings::EAxis translation_axes[3] = {
    JPH::SixDOFConstraintSettings::EAxis::TranslationX, JPH::SixDOFConstraintSettings::EAxis::TranslationY, JPH::SixDOFConstraintSettings::EAxis::TranslationZ };
const JPH::SixDOFConstraintSettings::EAxis rotation_axes[3] = {
    JPH::SixDOFConstraintSettings::EAxis::RotationX, JPH::SixDOFConstraintSettings::EAxis::RotationY, JPH::SixDOFConstraintSettings::EAxis::RotationZ };

}

void BodyDrivesSys::preinit() {
    world = &physicsWorld(reg);
    reg.on_destroy<BodyDrive>().connect<&BodyDrivesSys::driveDestroyed>(this);
    reg.on_destroy<RigidBody>().connect<&BodyDrivesSys::bodyDestroyed>(this);
    fixedSteps(reg).before.push_back({ this, [this](float h) { step(h); } });
}

void BodyDrivesSys::drop(entt::entity entity) {
    auto it = motors.find(entity);
    if(it == motors.end()) return;
    world->system().RemoveConstraint(it->second.constraint);
    motors.erase(it);
    if(reg.valid(entity)) {
        if(auto* drive = reg.try_get<BodyDrive>(entity)) drive->driven = false;
    }
}

void BodyDrivesSys::driveDestroyed(entt::registry&, entt::entity entity) {
    drop(entity);
}

void BodyDrivesSys::bodyDestroyed(entt::registry&, entt::entity entity) {
    // The body goes: its motor, and those anchored on it. The constraint is only taken off the list here, which
    // is safe whether the body is still there or not
    drop(entity);
    std::vector<entt::entity> anchored;
    for(auto& [driven, motor] : motors) {
        if(motor.anchor == entity) anchored.push_back(driven);
    }
    for(auto driven : anchored) drop(driven);
}

void BodyDrivesSys::step(float h) {

    auto& bodies = world->bodies();
    const auto& lock_interface = world->system().GetBodyLockInterfaceNoLock();

    for(auto [entity, drive, rigid] : reg.view<BodyDrive, RigidBody>().each()) {

        JPH::BodyID id = playerBodyId(reg, entity);
        JPH::BodyID anchor_id = drive.anchor != entt::null ? playerBodyId(reg, drive.anchor) : JPH::BodyID();
        bool ready = !id.IsInvalid() && rigid.motion == RigidBody::Motion::Dynamic && (drive.anchor == entt::null || !anchor_id.IsInvalid());

        auto it = motors.find(entity);
        if(it != motors.end() && (!ready || it->second.anchor != drive.anchor || it->second.rotate != drive.rotate)) {
            drop(entity);
            it = motors.end();
        }
        if(!ready) continue;

        if(it == motors.end()) {
            JPH::Body* body = lock_interface.TryGetBody(id);
            JPH::Body* anchor = drive.anchor != entt::null ? lock_interface.TryGetBody(anchor_id) : &JPH::Body::sFixedToWorld;
            if(!body || !anchor) continue;

            // Constraint frames: the anchor's origin with its axes, the body's centre of mass with its axes
            JPH::SixDOFConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
            settings.mPosition1 = drive.anchor != entt::null ? JPH::RVec3(-anchor->GetShape()->GetCenterOfMass()) : JPH::RVec3::sZero();
            settings.mPosition2 = JPH::RVec3::sZero();
            for(int a = 0; a < JPH::SixDOFConstraintSettings::EAxis::Num; a++) settings.MakeFreeAxis((JPH::SixDOFConstraintSettings::EAxis) a);

            Motor motor;
            motor.constraint = static_cast<JPH::SixDOFConstraint*>(settings.Create(*anchor, *body));
            motor.anchor = drive.anchor;
            motor.rotate = drive.rotate;
            for(auto axis : translation_axes) motor.constraint->SetMotorState(axis, JPH::EMotorState::Velocity);
            if(drive.rotate) {
                for(auto axis : rotation_axes) motor.constraint->SetMotorState(axis, JPH::EMotorState::Velocity);
            }
            world->system().AddConstraint(motor.constraint);
            it = motors.emplace(entity, std::move(motor)).first;
        }

        JPH::SixDOFConstraint& constraint = *it->second.constraint;
        for(auto axis : translation_axes) constraint.GetMotorSettings(axis).SetForceLimit(drive.max_force);
        for(auto axis : rotation_axes) constraint.GetMotorSettings(axis).SetTorqueLimit(drive.max_torque);

        glm::vec3 origin(0.f);
        glm::quat frame(1.f, 0.f, 0.f, 0.f);
        if(drive.anchor != entt::null) {
            origin = toGlm(JPH::Vec3(bodies.GetPosition(anchor_id)));
            frame = toGlm(bodies.GetRotation(anchor_id));
        }
        glm::vec3 centre = toGlm(JPH::Vec3(bodies.GetCenterOfMassPosition(id)));
        glm::quat rotation = toGlm(bodies.GetRotation(id));

        // Unity's Hand.JointMove: the controller's velocity while the hand is where it should be, and the error
        // closed at `gain` a step
        drive.error = origin + frame * drive.position - centre;
        float distance = glm::length(drive.error);
        glm::vec3 velocity = frame * drive.velocity * std::max(1.f - distance, 0.f) + drive.error * (drive.gain / h);
        constraint.SetTargetVelocityCS(toJolt(glm::inverse(frame) * velocity * linear_sign));

        if(drive.rotate) {
            glm::quat turn = glm::normalize((frame * drive.rotation) * glm::inverse(rotation));
            if(turn.w < 0.f) turn = -turn;
            float angle = 2.f * std::acos(std::clamp(turn.w, -1.f, 1.f));
            glm::vec3 axis(turn.x, turn.y, turn.z);
            float length = glm::length(axis);
            glm::vec3 error = length > 1e-6f ? axis / length * angle : glm::vec3(0.f);
            drive.angular_error = angle;
            glm::vec3 angular = frame * drive.angular_velocity * std::max(1.f - angle, 0.f) + error * (drive.angular_gain / h);
            // In the body's own frame (Jolt: rotation motors work in the constraint space of body 2)
            constraint.SetTargetAngularVelocityCS(toJolt(glm::inverse(rotation) * angular * angular_sign));
        }

        drive.driven = true;
        bodies.ActivateBody(id);
        if(drive.anchor != entt::null) bodies.ActivateBody(anchor_id);

    }

    // Drives whose component lost its RigidBody
    std::vector<entt::entity> gone;
    for(auto& [entity, motor] : motors) {
        if(!reg.valid(entity) || !reg.all_of<BodyDrive, RigidBody>(entity)) gone.push_back(entity);
    }
    for(auto entity : gone) drop(entity);

}

void BodyDrivesSys::finish() {
    reg.on_destroy<BodyDrive>().disconnect(this);
    reg.on_destroy<RigidBody>().disconnect(this);
    fixedSteps(reg).remove(this);
    for(auto& [entity, motor] : motors) world->system().RemoveConstraint(motor.constraint);
    motors.clear();
}
