#include "player_systems.h"

#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include "logic/components/camera.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/terrain/terrain_generator.h"
#include "physics/physics_sys.h"
#include "scene/component_registry.h"
#include "scene/system_registry.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace dy;

namespace {

// Everything but the bodies of one player (itself, its hands, what it holds) and sensors
class NotOfPlayer : public JPH::BodyFilter {
public:
    NotOfPlayer(const JPH::GroupFilter* filter, uint32_t group) : filter(filter), group(group) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        if(body.IsSensor()) return false;
        const JPH::CollisionGroup& g = body.GetCollisionGroup();
        return !(g.GetGroupFilter() == filter && g.GetGroupID() == group);
    }

private:
    const JPH::GroupFilter* filter;
    uint32_t group;
};

}



// Makers

entt::entity dy::makeFPSPlayer(entt::registry& reg, glm::vec3 eyes, float yaw) {

    entt::entity player = reg.create();
    reg.emplace<Transform>(player, eyes, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(1.f));

    // Unity's Body: a capsule over the upper half of the body, its top at the eyes; the legs are the stand force
    auto& body = reg.emplace<RigidBody>(player);
    body.shape = RigidBody::Shape::Capsule;
    body.half_extent = glm::vec3(0.108f, 0.342f, 0.f);
    body.offset = glm::vec3(0.f, -0.45f, 0.f);
    body.mass = 70.f;
    body.lock_rotation = true;
    body.friction = 0.2f;
    body.restitution = 0.f;

    reg.emplace<MouseKeyboardInput>(player);
    reg.emplace<ControllersInput>(player);
    reg.emplace<PlayerView>(player).yaw = yaw;
    reg.emplace<PlayerLegs>(player);
    reg.emplace<LookTurn>(player);
    reg.emplace<DesktopView>(player);
    reg.emplace<Holder>(player);
    reg.emplace<Rider>(player);
    return player;

}

entt::entity dy::makeVRPlayer(entt::registry& reg, glm::vec3 eyes, float yaw) {

    entt::entity player = reg.create();
    reg.emplace<Transform>(player, eyes, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(1.f));

    auto& body = reg.emplace<RigidBody>(player);
    body.shape = RigidBody::Shape::Capsule;
    body.half_extent = glm::vec3(0.108f, 0.342f, 0.f);
    body.offset = glm::vec3(0.f, -0.45f, 0.f);
    body.mass = 70.f;
    body.lock_rotation = true;
    body.friction = 0.2f;
    body.restitution = 0.f;

    reg.emplace<VRPlayerInput>(player);
    reg.emplace<PlayerView>(player).yaw = yaw;
    reg.emplace<PlayerLegs>(player);
    reg.emplace<VRTracking>(player);
    auto& vr = reg.emplace<VRBody>(player);
    vr.yaw = yaw;
    vr.head = glm::vec3(0.f, 1.8f, 0.f);

    // Unity's Hand prefab: 1.5 kg, with gravity
    const glm::vec3 half(0.04f, 0.05f, 0.09f);
    for(int side = 0; side < 2; side++) {
        entt::entity e = reg.create();
        auto& hand = reg.emplace<Hand>(e);
        hand.player = player;
        hand.side = side;
        hand.rest = glm::vec3(side == VRInput::left ? -0.22f : 0.22f, -0.45f, -0.3f);
        reg.emplace<Transform>(e, eyes + vr.rotation() * hand.rest, vr.rotation(), half * 2.f);
        auto& hand_body = reg.emplace<RigidBody>(e);
        hand_body.half_extent = half;
        hand_body.mass = 1.5f;
        hand_body.restitution = 0.f;
        auto& drive = reg.emplace<BodyDrive>(e);
        drive.anchor = player;
        drive.position = vr.rotation() * hand.rest;
        drive.rotation = vr.rotation();
        reg.emplace<Grabber>(e);
        vr.hands[side] = e;
    }
    return player;

}



// PlayerSpawn

PlayerSpawnSys::PlayerSpawnSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    vr = params.value("kind", std::string("fps")) == "vr";
    if(auto it = params.find("position"); it != params.end() && it->is_array() && it->size() == 2) {
        position = glm::vec2(it->at(0).get<float>(), it->at(1).get<float>());
    }
    yaw = params.value("yaw", yaw);
    props = params.value("props", props);
    flat = params.value("ground", flat);
    third_person = params.value("third_person", third_person);
}

void PlayerSpawnSys::preinit() {
    // Before RigidBody's init, which makes the grounds
    if(flat) reg.emplace<Ground>(reg.create()).size = glm::vec3(1200.f, 10.f, 1200.f);
}

void PlayerSpawnSys::init() {

    auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>();
    auto ground = [&](float x, float z) { return generator ? (*generator)->surfaceHeight(x, z) : 0.f; };

    glm::vec3 eyes(position.x, ground(position.x, position.y) + 1.8f, position.y);
    if(vr) makeVRPlayer(reg, eyes, yaw);
    else {
        entt::entity player = makeFPSPlayer(reg, eyes, yaw);
        if(third_person > 0.f) reg.emplace<ThirdPerson>(player).distance = third_person;
    }

    // Boxes in an arc in front of the player: light ones to throw, a heavy one that weighs on the arm
    glm::quat turn = glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f));
    for(int i = 0; i < props; i++) {
        float angle = props > 1 ? glm::radians(-50.f + 100.f * i / (props - 1)) : 0.f;
        float size = 0.15f + 0.07f * (i % 4);
        glm::vec3 at = glm::vec3(position.x, 0.f, position.y) + turn * (glm::angleAxis(-angle, glm::vec3(0.f, 1.f, 0.f)) * glm::vec3(0.f, 0.f, -(0.9f + 0.25f * (i % 3))));
        at.y = ground(at.x, at.z) + 0.6f + size;
        entt::entity e = reg.create();
        reg.emplace<Transform>(e, at, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(size));
        auto& body = reg.emplace<RigidBody>(e);
        body.half_extent = glm::vec3(size * 0.5f);
        body.mass = i % 4 == 3 ? 40.f : 300.f * size * size * size;
        reg.emplace<Prop>(e).color = i % 4 == 3 ? glm::vec4(0.35f, 0.35f, 0.4f, 1.f) : glm::vec4(0.9f, 0.6f + 0.1f * (i % 3), 0.25f, 1.f);
    }

}



// LookTurn

void LookTurnSys::tick(double dt) {
    for(auto [entity, look, view] : reg.view<LookTurn, PlayerView>().each()) {
        const GenericPlayerInput& input = playerInput(reg, entity);
        glm::vec2 turn = input.viewIsDelta() ? input.getViewAxis() : input.getViewAxis() * look.stick_speed * (float) dt;
        view.yaw -= turn.x;
        view.pitch = std::clamp(view.pitch + turn.y, -1.55f, 1.55f);
    }
}



// PlayerLegs

void PlayerLegsSys::preinit() {
    world = &physicsWorld(reg);
    fixedSteps(reg).before.push_back({ this, [this](float h) { step(h); } });
}

void PlayerLegsSys::tick(double dt) {
    // The ground under the legs has its colliders before the step, wherever the body's capsule is
    auto* needs = reg.ctx().find<TerrainNeeds>();
    if(!needs) needs = &reg.ctx().emplace<TerrainNeeds>();
    for(auto [entity, legs, transform] : reg.view<PlayerLegs, Transform>().each()) {
        glm::vec3 reach(1.5f, 0.f, 1.5f);
        needs->boxes.push_back({ transform.position - reach - glm::vec3(0.f, legs.height + legs.reach + 1.5f, 0.f), transform.position + reach });
    }
}

void PlayerLegsSys::step(float h) {

    auto& bodies = world->bodies();
    auto& collision = playerCollision(reg);

    for(auto [entity, legs, view, rigid] : reg.view<PlayerLegs, PlayerView, RigidBody>().each()) {

        const GenericPlayerInput& input = playerInput(reg, entity);
        const glm::vec2 move = input.getMovementAxis();
        const bool jump = input.getJumpButton();
        if(!jump) legs.jump_locked = false;

        JPH::BodyID id = playerBodyId(reg, entity);
        if(id.IsInvalid()) continue;
        if(legs.group == 0) {
            legs.group = collision.next_group++;
            setPlayerGroup(reg, *world, id, legs.group, PlayerGroups::body);
        }

        legs.applied_force = glm::vec3(0.f);
        legs.standing = false;
        legs.walking = false;
        legs.ground = entt::null;
        legs.ground_velocity = glm::vec3(0.f);
        if(legs.limp) continue;

        bodies.ActivateBody(id);
        const float mass = bodyMass(*world, id);
        const glm::vec3 eyes = toGlm(JPH::Vec3(bodies.GetPosition(id)));
        const glm::vec3 velocity = toGlm(bodies.GetLinearVelocity(id));
        const float height = std::max(legs.height, 0.3f);

        // Stand: what is under the eyes within the height pushes the body up to it
        JPH::RRayCast ray(JPH::RVec3(toJolt(eyes)), JPH::Vec3(0.f, -(height + legs.reach), 0.f));
        JPH::RayCastResult hit;
        NotOfPlayer filter(collision.filter, legs.group);
        if(world->system().GetNarrowPhaseQueryNoLock().CastRay(ray, hit, {}, {}, filter)) {
            legs.standing = true;
            legs.ground_distance = hit.mFraction * (height + legs.reach);
            glm::vec3 point = eyes - glm::vec3(0.f, legs.ground_distance, 0.f);
            legs.ground = bodyEntity(reg, *world, hit.mBodyID);
            if(bodies.GetMotionType(hit.mBodyID) != JPH::EMotionType::Static) {
                legs.ground_velocity = toGlm(bodies.GetPointVelocity(hit.mBodyID, JPH::RVec3(toJolt(point))));
            }

            float force = legs.stand_force * (legs.stand_speed * 2.f * std::min(0.5f, height - legs.ground_distance) + legs.ground_velocity.y - velocity.y);
            force = std::min(force, legs.leg_force);
            if(std::isfinite(force) && force > 0.f) {
                glm::vec3 real(0.f, force * mass / 10.f, 0.f);
                bodies.AddForce(id, toJolt(real));
                legs.applied_force += real;
            }
        }

        // Walk: a force toward the speed asked along the view's heading
        {
            glm::vec3 forward(-std::sin(view.yaw), 0.f, -std::cos(view.yaw));
            glm::vec3 right(std::cos(view.yaw), 0.f, -std::sin(view.yaw));
            glm::vec3 axe = move.x * right + move.y * forward;
            if(glm::dot(axe, axe) > 1.f) axe = glm::normalize(axe);

            glm::vec3 relative = velocity - legs.ground_velocity;
            if(glm::dot(axe, axe) != 0.f) {
                // Not braked for going faster than asked the other way
                float along = glm::dot(relative, axe);
                if(along < 0.f) relative -= along * axe;
            }
            glm::vec3 force = legs.walk_force * (axe * (input.getRunButton() ? legs.sprint_speed : legs.walk_speed) - relative);
            if(!legs.standing) force *= legs.air_walk;
            glm::vec3 real = glm::vec3(force.x, 0.f, force.z) * mass / 10.f;
            bodies.AddForce(id, toJolt(real));
            legs.applied_force += real;
            legs.walking = glm::dot(axe, axe) > 0.001f && legs.standing;
        }

        // Jump
        if(legs.standing && jump && !legs.jump_locked) {
            float force = std::min(legs.jump_force * (legs.jump_speed + legs.ground_velocity.y - velocity.y), legs.leg_force);
            glm::vec3 real(0.f, force * mass / 10.f, 0.f);
            bodies.AddForce(id, toJolt(real));
            legs.applied_force += real;
        }

    }

}

void PlayerLegsSys::finish() {
    fixedSteps(reg).remove(this);
}



// PlayerViewpoint

void PlayerViewpointSys::preinit() {
    if(!reg.ctx().find<Camera>()) reg.ctx().emplace<Camera>();
}

void PlayerViewpointSys::tick(double dt) {

    for(auto [entity, transform, view] : reg.view<DesktopView, Transform, PlayerView>().each()) {
        auto& camera = reg.ctx().get<Camera>();
        camera.position = transform.position;
        if(auto* behind = reg.try_get<ThirdPerson>(entity)) {
            camera.position += (glm::vec3(0.f, behind->height, 0.f) - view.forward()) * behind->distance;
        }
        camera.yaw = view.yaw;
        camera.pitch = view.pitch;
    }

    // The play area sits where it puts the head on the body
    if(auto* rig = reg.ctx().find<VRRig>()) {
        for(auto [entity, transform, vr, tracking] : reg.view<Transform, VRBody, VRTracking>().each()) {
            rig->yaw = vr.yaw;
            rig->position = transform.position - vr.rotation() * vr.head;
            rig->stick_locomotion = false;
        }
    }

}



// Registration

void dy::registerPlayer() {

    registerSystem("PlayerSpawn", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<PlayerSpawnSys>(reg, params); }, SystemScope::Host });
    registerSystem("VRPlayerInput", systemInfo<VRPlayerInputSys>(SystemScope::Host));
    registerSystem("LookTurn", systemInfo<LookTurnSys>(SystemScope::Host));
    registerSystem("VRBody", systemInfo<VRBodySys>(SystemScope::Host));
    registerSystem("PlayerLegs", systemInfo<PlayerLegsSys>(SystemScope::Host));
    registerSystem("VRHands", systemInfo<VRHandsSys>(SystemScope::Host));
    registerSystem("Grab", systemInfo<GrabSys>(SystemScope::Host));
    registerSystem("Hold", systemInfo<HoldSys>(SystemScope::Host));
    registerSystem("BodyDrives", systemInfo<BodyDrivesSys>(SystemScope::Host));
    registerSystem("PlayerViewpoint", systemInfo<PlayerViewpointSys>(SystemScope::Host));
    registerMounts();

}
