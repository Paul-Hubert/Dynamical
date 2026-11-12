#include "player_systems.h"

#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "physics/physics_sys.h"

#include <algorithm>
#include <cmath>

using namespace dy;

// VRPlayerInput (Unity's VRPlayerInput: the left stick walks, the right one turns, both past half their travel)

void VRPlayerInputSys::tick(double dt) {

    auto* vr = reg.ctx().find<VRInput>();
    if(!vr) return;
    time += dt;

    for(auto [entity, body, tracking, input] : reg.view<VRBody, VRTracking, VRPlayerInput>().each()) {

        tracking.focused = vr->session_running && vr->focused;
        tracking.head = vr->head;
        for(int i = 0; i < 2; i++) {
            auto& from = vr->hands[i];
            auto& to = tracking.hands[i];
            to.pose = from.pose;
            to.velocity = from.linear_velocity;
            to.angular_velocity = from.angular_velocity;
            to.grip = from.grip;
            to.trigger = from.trigger;
        }

        // The controllers as they are: the component answers for them
        input.focused = tracking.focused;
        input.left_stick = vr->hands[VRInput::left].thumbstick;
        input.right_stick = vr->hands[VRInput::right].thumbstick;
        input.left_stick_click = vr->hands[VRInput::left].thumbstick_click;
        input.right_primary = vr->hands[VRInput::right].primary;
        input.left_trigger = vr->hands[VRInput::left].trigger;
        input.right_trigger = vr->hands[VRInput::right].trigger;
        if(input.focused && (glm::length(input.left_stick) > 0.5f || glm::length(input.right_stick) > 0.5f || input.left_stick_click
            || input.right_primary || input.left_trigger > 0.5f || input.right_trigger > 0.5f)) input.last_action = time;

    }

}



// VRBody

void VRBodySys::preinit() {
    world = &physicsWorld(reg);
}

void VRBodySys::tick(double dt) {

    for(auto [entity, vr, tracking, view, legs, transform] : reg.view<VRBody, VRTracking, PlayerView, PlayerLegs, Transform>().each()) {

        const GenericPlayerInput& input = playerInput(reg, entity);

        // Unity's TrackFocus: poses are held without focus, and over the two frames after it comes back: the first
        // is skipped, the second only re-bases the head, the third moves by what the head did since the second
        bool focused = tracking.focused;
        if(focused && !vr.was_focused && vr.seen_focus) vr.refocus_frame = 1;
        else if(vr.refocus_frame > 0) vr.refocus_frame++;
        if(vr.refocus_frame > 3) vr.refocus_frame = 0;
        if(focused) vr.seen_focus = true;
        vr.pose_hold = !focused || (vr.refocus_frame > 0 && vr.refocus_frame < 3);
        vr.was_focused = focused;

        // Move: the body goes where the head goes in the room
        if(tracking.head.valid) {
            if(vr.pose_hold) {
                if(vr.refocus_frame == 2) vr.head = tracking.head.position;
            } else if(!vr.tracked) {
                // The first pose places the play area, not the body
                vr.head = tracking.head.position;
                vr.tracked = true;
            } else {
                glm::vec3 delta = vr.rotation() * (tracking.head.position - vr.head);
                vr.head = tracking.head.position;
                transform.position += delta;
                if(auto* pose = reg.try_get<PhysicsPose>(entity)) {
                    pose->prev_position += delta;
                    pose->position += delta;
                }
                JPH::BodyID id = playerBodyId(reg, entity);
                if(!id.IsInvalid()) {
                    auto& bodies = world->bodies();
                    bodies.SetPosition(id, bodies.GetPosition(id) + toJolt(delta), JPH::EActivation::Activate);
                }
            }
        }

        // Turn, around the head
        if(!input.viewIsDelta()) vr.yaw -= input.getViewAxis().x * vr.turn_speed * (float) dt;

        // The legs hold the head where the player holds it
        legs.height = std::max(vr.standing_height + vr.head.y - 1.8f, 0.3f);

        // The walk follows the head
        glm::vec3 forward = vr.rotation() * (tracking.head.valid ? tracking.head.rotation * glm::vec3(0.f, 0.f, -1.f) : glm::vec3(0.f, 0.f, -1.f));
        if(glm::length(glm::vec2(forward.x, forward.z)) > 0.01f) view.yaw = std::atan2(-forward.x, -forward.z);
        view.pitch = std::asin(std::clamp(forward.y, -1.f, 1.f));

    }

}



// VRHands

void VRHandsSys::preinit() {
    world = &physicsWorld(reg);
    fixedSteps(reg).before.push_back({ this, [this](float h) { step(h); } });
}

void VRHandsSys::step(float h) {

    auto& bodies = world->bodies();

    for(auto [entity, hand, drive, transform] : reg.view<Hand, BodyDrive, Transform>().each()) {

        if(hand.player == entt::null || !reg.valid(hand.player)) continue;
        auto* vr = reg.try_get<VRBody>(hand.player);
        auto* tracking = reg.try_get<VRTracking>(hand.player);
        auto* legs = reg.try_get<PlayerLegs>(hand.player);
        JPH::BodyID id = playerBodyId(reg, entity);
        JPH::BodyID player_id = playerBodyId(reg, hand.player);
        if(!vr || !tracking || !legs || id.IsInvalid() || player_id.IsInvalid()) continue;

        // Hands and body of a player do not collide (Unity: the joint between them)
        if(legs->group != 0 && bodies.GetCollisionGroup(id).GetGroupFilter() == nullptr) {
            setPlayerGroup(reg, *world, id, legs->group, hand.side == VRInput::left ? PlayerGroups::left_hand : PlayerGroups::right_hand);
        }

        // The controller, when it can be trusted: the last trusted state stands otherwise
        const auto& controller = tracking->hands[hand.side];
        if(tracking->focused && !vr->pose_hold && controller.pose.valid) {
            hand.tracked = true;
            hand.position = controller.pose.position - vr->head;
            hand.rotation = controller.pose.rotation;
            hand.velocity = controller.velocity;
            hand.angular_velocity = controller.angular_velocity;
            hand.grip = controller.grip;
            hand.trigger = controller.trigger;
        }

        // Where the controller is from the head, turned with the play area
        glm::quat rig = vr->rotation();
        drive.anchor = hand.player;
        if(hand.tracked) {
            drive.position = rig * hand.position;
            drive.rotation = rig * hand.rotation;
            drive.velocity = rig * hand.velocity;
            drive.angular_velocity = rig * hand.angular_velocity;
        } else {
            drive.position = rig * hand.rest;
            drive.rotation = rig;
            drive.velocity = drive.angular_velocity = glm::vec3(0.f);
        }

        // Failsafe: too far from where it is asked, the hand is put there
        glm::vec3 player_position = toGlm(JPH::Vec3(bodies.GetPosition(player_id)));
        glm::vec3 target = player_position + drive.position;
        glm::vec3 position = toGlm(JPH::Vec3(bodies.GetPosition(id)));
        if(glm::length(target - position) > hand.failsafe) {
            glm::vec3 player_velocity = toGlm(bodies.GetLinearVelocity(player_id));
            bodies.SetPositionAndRotation(id, JPH::RVec3(toJolt(target)), toJolt(glm::normalize(drive.rotation)), JPH::EActivation::Activate);
            bodies.SetLinearAndAngularVelocity(id, toJolt(player_velocity + drive.velocity), toJolt(drive.angular_velocity));
            if(auto* pose = reg.try_get<PhysicsPose>(entity)) {
                pose->prev_position = pose->position = target;
                pose->prev_rotation = pose->rotation = drive.rotation;
            }
            hand.teleports++;
        }

        // What the legs do to the body, they do to the hands (Unity's ApplyForcesToHands)
        float player_mass = bodyMass(*world, player_id);
        if(player_mass > 0.f) bodies.AddForce(id, toJolt(legs->applied_force * (bodyMass(*world, id) / player_mass)));

    }

}

void VRHandsSys::finish() {
    fixedSteps(reg).remove(this);
}
