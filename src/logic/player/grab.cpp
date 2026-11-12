#include "player_systems.h"

#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "physics/physics_sys.h"

#include <algorithm>
#include <cmath>

using namespace dy;

namespace {

// What a hand may grab: not its player's body or hands, not what the player's own hold carries
class Grabbable : public JPH::BodyFilter {
public:
    Grabbable(const JPH::GroupFilter* filter, uint32_t group) : filter(filter), group(group) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        if(body.IsSensor()) return false;
        const JPH::CollisionGroup& g = body.GetCollisionGroup();
        if(g.GetGroupFilter() != filter || g.GetGroupID() != group) return true;
        // What the other hand holds can be taken with this one too
        auto sub = g.GetSubGroupID();
        return sub == PlayerGroups::held_left || sub == PlayerGroups::held_right || sub == PlayerGroups::held_both;
    }

private:
    const JPH::GroupFilter* filter;
    uint32_t group;
};

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

uint32_t groupOf(const entt::registry& reg, entt::entity player) {
    if(player == entt::null || !reg.valid(player)) return 0;
    auto* legs = reg.try_get<PlayerLegs>(player);
    return legs ? legs->group : 0;
}

}



// Grab

void GrabSys::preinit() {
    world = &physicsWorld(reg);
    reg.on_destroy<RigidBody>().connect<&GrabSys::bodyDestroyed>(this);
    fixedSteps(reg).before.push_back({ this, [this](float h) { step(h); } });
}

void GrabSys::step(float h) {

    time += h;

    for(auto [entity, hand, grabber] : reg.view<Hand, Grabber>().each()) {

        grabber.grabbing = hand.grip > grabber.grip_threshold;

        auto it = grips.find(entity);
        if(it != grips.end()) {
            // The failsafe moved the hand: what it held stays behind
            bool teleported = it->second.teleports != hand.teleports;
            if(teleported || !grabber.grabbing) release(entity, !teleported);
        }
        if(grabber.grabbing && !grabber.grabbed) grab(entity, hand, grabber);

    }

    // Hands and what they let go collide again a moment later
    for(size_t i = 0; i < regroups.size();) {
        if(regroups[i].time > time) { i++; continue; }
        entt::entity object = regroups[i].object;
        regroups.erase(regroups.begin() + i);
        if(reg.valid(object) && !reg.all_of<Grabbed>(object) && !reg.all_of<Held>(object)) {
            JPH::BodyID id = playerBodyId(reg, object);
            if(!id.IsInvalid()) clearPlayerGroup(*world, id);
        }
    }

}

void GrabSys::grab(entt::entity hand_entity, Hand& hand, Grabber& grabber) {

    JPH::BodyID id = playerBodyId(reg, hand_entity);
    uint32_t group = groupOf(reg, hand.player);
    if(id.IsInvalid() || group == 0) return;

    const auto& lock_interface = world->system().GetBodyLockInterfaceNoLock();
    JPH::Body* body = lock_interface.TryGetBody(id);
    if(!body) return;

    // What the hand touches (Unity: OnCollisionStay), the deepest contact first
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = grabber.touch;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    Grabbable filter(playerCollision(reg).filter, group);
    JPH::RVec3 base = body->GetCenterOfMassPosition();
    world->system().GetNarrowPhaseQueryNoLock().CollideShape(body->GetShape(), JPH::Vec3::sReplicate(1.f), body->GetCenterOfMassTransform(),
        settings, base, collector, {}, {}, filter);
    collector.Sort();   // by early out fraction: deepest first

    for(const auto& hit : collector.mHits) {
        if(hit.mBodyID2 == id) continue;
        entt::entity object = bodyEntity(reg, *world, hit.mBodyID2);
        if(object != entt::null && reg.all_of<Ungrabbable>(object)) continue;
        JPH::Body* other = lock_interface.TryGetBody(hit.mBodyID2);
        if(!other) continue;

        // Locked where they touch (Unity: a joint with every motion locked, anchored at the contact)
        JPH::FixedConstraintSettings fixed;
        fixed.mSpace = JPH::EConstraintSpace::WorldSpace;
        fixed.mPoint1 = fixed.mPoint2 = base + hit.mContactPointOn2;
        // The hand's motor pulls with up to 10000 N on 1.5 kg: the lock is solved last and longer, or it gives
        fixed.mConstraintPriority = 1000;
        fixed.mNumVelocityStepsOverride = 40;
        fixed.mNumPositionStepsOverride = 8;
        Grip grip;
        grip.constraint = static_cast<JPH::FixedConstraint*>(fixed.Create(*body, *other));
        grip.body = hit.mBodyID2;
        grip.object = object;
        grip.teleports = hand.teleports;
        world->system().AddConstraint(grip.constraint);
        grips[hand_entity] = std::move(grip);

        grabber.grabbed = true;
        grabber.object = object;
        if(object != entt::null && other->IsDynamic()) {
            reg.get_or_emplace<Grabbed>(object).hands[hand.side] = hand_entity;
            regroup(object);
        }
        return;
    }

}

void GrabSys::regroup(entt::entity object) {

    JPH::BodyID id = playerBodyId(reg, object);
    if(id.IsInvalid()) return;
    auto* grabbed = reg.try_get<Grabbed>(object);
    if(!grabbed) return;

    bool left = grabbed->hands[VRInput::left] != entt::null, right = grabbed->hands[VRInput::right] != entt::null;
    entt::entity hand = left ? grabbed->hands[VRInput::left] : grabbed->hands[VRInput::right];
    uint32_t group = reg.valid(hand) && reg.all_of<Hand>(hand) ? groupOf(reg, reg.get<Hand>(hand).player) : 0;
    if(group == 0) return;
    setPlayerGroup(reg, *world, id, group, left && right ? PlayerGroups::held_both : left ? PlayerGroups::held_left : PlayerGroups::held_right);

}

void GrabSys::release(entt::entity hand_entity, bool thrown) {

    auto it = grips.find(hand_entity);
    if(it == grips.end()) return;
    Grip grip = std::move(it->second);
    grips.erase(it);
    world->system().RemoveConstraint(grip.constraint);

    Hand* hand = reg.valid(hand_entity) ? reg.try_get<Hand>(hand_entity) : nullptr;
    Grabber* grabber = reg.valid(hand_entity) ? reg.try_get<Grabber>(hand_entity) : nullptr;
    if(grabber) {
        grabber->grabbed = false;
        grabber->object = entt::null;
    }

    entt::entity object = grip.object;
    if(object == entt::null || !reg.valid(object)) return;
    JPH::BodyID id = playerBodyId(reg, object);
    if(id.IsInvalid() || id != grip.body) return;

    auto& bodies = world->bodies();
    if(auto* grabbed = reg.try_get<Grabbed>(object); grabbed && hand) {
        grabbed->hands[hand->side] = entt::null;
        if(grabbed->hands[0] == entt::null && grabbed->hands[1] == entt::null) {
            reg.remove<Grabbed>(object);
            regroups.push_back({ time + (grabber ? grabber->collision_delay : 0.1f), object });
        } else {
            regroup(object);
        }
    }

    // Unity's Thrower: the body leaves with the player's velocity and the controller's
    if(thrown && hand && grabber && bodies.GetMotionType(id) == JPH::EMotionType::Dynamic && reg.valid(hand->player)) {
        JPH::BodyID player_id = playerBodyId(reg, hand->player);
        auto* vr = reg.try_get<VRBody>(hand->player);
        if(!player_id.IsInvalid() && vr) {
            glm::vec3 velocity = toGlm(bodies.GetLinearVelocity(player_id)) + vr->rotation() * hand->velocity;
            bodies.SetLinearAndAngularVelocity(id, toJolt(velocity * grabber->velocity_multiplier),
                bodies.GetAngularVelocity(id) * grabber->angular_velocity_multiplier);
        }
    }

}

void GrabSys::bodyDestroyed(entt::registry&, entt::entity entity) {
    // A hand, or what a hand holds, goes: the constraint is taken off, nothing is thrown
    std::vector<entt::entity> hands;
    for(auto& [hand, grip] : grips) {
        if(hand == entity || grip.object == entity) hands.push_back(hand);
    }
    for(auto hand : hands) {
        auto it = grips.find(hand);
        world->system().RemoveConstraint(it->second.constraint);
        grips.erase(it);
        if(reg.valid(hand)) {
            if(auto* grabber = reg.try_get<Grabber>(hand)) {
                grabber->grabbed = false;
                grabber->object = entt::null;
            }
        }
    }
}

void GrabSys::finish() {
    reg.on_destroy<RigidBody>().disconnect(this);
    fixedSteps(reg).remove(this);
    for(auto& [hand, grip] : grips) world->system().RemoveConstraint(grip.constraint);
    grips.clear();
}



// Hold

void HoldSys::preinit() {
    world = &physicsWorld(reg);
    fixedSteps(reg).before.push_back({ this, [this](float h) { step(h); } });
}

void HoldSys::tick(double dt) {

    for(auto [entity, holder, view] : reg.view<Holder, PlayerView>().each()) {

        const GenericPlayerInput& input = playerInput(reg, entity);
        const bool interact = input.getInteractButton(), primary = input.getPrimaryActionButton();
        const bool interact_pressed = interact && !holder.was_interact, primary_pressed = primary && !holder.was_primary;
        holder.was_interact = interact;
        holder.was_primary = primary;

        if(holder.held != entt::null && (!reg.valid(holder.held) || !reg.all_of<RigidBody, BodyDrive, Held>(holder.held))) holder.held = entt::null;

        holder.distance = std::clamp(holder.distance + input.getControlAxis() * holder.wheel_step, holder.min_distance, holder.max_distance);

        if(interact_pressed) {
            if(holder.held != entt::null) drop(entity, holder, nullptr);
            else pick(entity, holder);
        } else if(primary_pressed && holder.held != entt::null) {
            JPH::BodyID id = playerBodyId(reg, entity);
            glm::vec3 velocity = view.forward() * holder.throw_speed;
            if(!id.IsInvalid()) velocity += toGlm(world->bodies().GetLinearVelocity(id));
            drop(entity, holder, &velocity);
        }

    }

}

void HoldSys::pick(entt::entity player, Holder& holder) {

    JPH::BodyID player_id = playerBodyId(reg, player);
    uint32_t group = groupOf(reg, player);
    auto& view = reg.get<PlayerView>(player);
    if(player_id.IsInvalid() || group == 0) return;

    auto& bodies = world->bodies();
    glm::vec3 eyes = toGlm(JPH::Vec3(bodies.GetPosition(player_id)));
    JPH::RRayCast ray(JPH::RVec3(toJolt(eyes)), toJolt(view.forward() * holder.reach));
    JPH::RayCastResult hit;
    NotOfPlayer filter(playerCollision(reg).filter, group);
    if(!world->system().GetNarrowPhaseQueryNoLock().CastRay(ray, hit, {}, {}, filter)) return;

    entt::entity object = bodyEntity(reg, *world, hit.mBodyID);
    if(object == entt::null || bodies.GetMotionType(hit.mBodyID) != JPH::EMotionType::Dynamic) return;
    if(reg.any_of<Ungrabbable, Held, Grabbed, BodyDrive>(object)) return;

    // The body is asked to where it is, then brought in front of the eyes by the drive
    auto& drive = reg.emplace<BodyDrive>(object);
    drive.anchor = player;
    drive.position = toGlm(JPH::Vec3(bodies.GetCenterOfMassPosition(hit.mBodyID))) - eyes;
    drive.rotation = toGlm(bodies.GetRotation(hit.mBodyID));
    drive.max_force = holder.max_force;
    drive.max_torque = holder.max_torque;
    reg.emplace<Held>(object).by = player;
    holder.held = object;
    holder.held_rotation = glm::inverse(view.rotation()) * drive.rotation;
    setPlayerGroup(reg, *world, hit.mBodyID, group, PlayerGroups::held_body);

}

void HoldSys::drop(entt::entity player, Holder& holder, const glm::vec3* velocity) {

    entt::entity object = holder.held;
    holder.held = entt::null;
    if(object == entt::null || !reg.valid(object)) return;

    reg.remove<BodyDrive>(object);
    reg.remove<Held>(object);
    JPH::BodyID id = playerBodyId(reg, object);
    if(id.IsInvalid()) return;
    clearPlayerGroup(*world, id);
    if(velocity) world->bodies().SetLinearVelocity(id, toJolt(*velocity));

}

void HoldSys::step(float h) {

    for(auto [entity, holder, view] : reg.view<Holder, PlayerView>().each()) {

        if(holder.held == entt::null) continue;
        if(!reg.valid(holder.held) || !reg.all_of<RigidBody, BodyDrive, Held>(holder.held)) {
            holder.held = entt::null;
            continue;
        }
        auto& drive = reg.get<BodyDrive>(holder.held);

        // Stuck behind something: let go
        if(drive.driven && glm::length(drive.error) > holder.break_distance) {
            drop(entity, holder, nullptr);
            continue;
        }

        // In front of the eyes, turning with the view (Unity's Weapon: the joint's target follows the camera)
        drive.position = view.forward() * holder.distance;
        drive.rotation = view.rotation() * holder.held_rotation;

        // Not given the legs' forces as the hands are (Unity's WeaponFollowsBody): standing would cancel its
        // weight, and what is held has to weigh on the drive

    }

}

void HoldSys::finish() {
    fixedSteps(reg).remove(this);
}
