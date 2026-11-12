#include <doctest/doctest.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <cmath>
#include <memory>
#include <vector>

#include "logic/components/camera.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/player/player.h"
#include "logic/player/player_systems.h"
#include "physics/physics_sys.h"
#include "physics/physics_world.h"

using namespace dy;

namespace {

// A flat ground and the player systems in the order of the scenes, without a terrain or a headset: the tests write
// a ScriptedInput and VRTracking themselves
struct PlayerWorld {
    entt::registry reg;
    std::vector<std::unique_ptr<System>> systems;
    static constexpr double dt = 1. / 60. + 1e-7;

    PlayerWorld() {
        reg.emplace<Ground>(reg.create());
        systems.push_back(std::make_unique<LookTurnSys>(reg));
        systems.push_back(std::make_unique<VRBodySys>(reg));
        systems.push_back(std::make_unique<PlayerLegsSys>(reg));
        systems.push_back(std::make_unique<VRHandsSys>(reg));
        systems.push_back(std::make_unique<GrabSys>(reg));
        systems.push_back(std::make_unique<HoldSys>(reg));
        systems.push_back(std::make_unique<BodyDrivesSys>(reg));
        systems.push_back(std::make_unique<RigidBodySys>(reg));
        systems.push_back(std::make_unique<PhysicsSys>(reg));
        systems.push_back(std::make_unique<PlayerViewpointSys>(reg));
        for(auto& s : systems) s->preinit();
        for(auto& s : systems) s->init();
    }

    ~PlayerWorld() {
        for(auto& s : systems) s->finish();
        systems.clear();
    }

    void frames(int n) {
        for(int i = 0; i < n; i++) {
            for(auto& s : systems) s->tick(dt);
        }
    }

    // What the test asks of a player, in place of its devices
    ScriptedInput& input(entt::entity e) { return reg.get_or_emplace<ScriptedInput>(e); }

    JPH::BodyInterface& bodies() { return reg.ctx().get<PhysicsWorld>().bodies(); }
    JPH::BodyID id(entt::entity e) { return JPH::BodyID(reg.get<RigidBody>(e).body); }
    glm::vec3 position(entt::entity e) { return toGlm(JPH::Vec3(bodies().GetPosition(id(e)))); }
    glm::quat rotation(entt::entity e) { return toGlm(bodies().GetRotation(id(e))); }
    glm::vec3 velocity(entt::entity e) { return toGlm(bodies().GetLinearVelocity(id(e))); }

    entt::entity box(glm::vec3 at, float size, float mass) {
        entt::entity e = reg.create();
        reg.emplace<Transform>(e, at, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(size));
        auto& body = reg.emplace<RigidBody>(e);
        body.half_extent = glm::vec3(size * 0.5f);
        body.mass = mass;
        return e;
    }
};

// A headset standing still at the origin of the play area, hands in front of the chest
void track(PlayerWorld& w, entt::entity player, glm::vec3 head = glm::vec3(0.f, 1.8f, 0.f)) {
    auto& t = w.reg.get<VRTracking>(player);
    t.focused = true;
    t.head.valid = true;
    t.head.position = head;
    for(int i = 0; i < 2; i++) {
        t.hands[i].pose.valid = true;
        t.hands[i].pose.position = head + glm::vec3(i == 0 ? -0.2f : 0.2f, -0.4f, -0.35f);
    }
}

float angleBetween(glm::quat a, glm::quat b) {
    return 2.f * std::acos(std::min(1.f, std::abs(glm::dot(glm::normalize(a), glm::normalize(b)))));
}

}

TEST_SUITE("player") {

TEST_CASE("PLAYER.1 the legs stand the body at its height, a little under where the stand force meets its weight") {

    PlayerWorld w;
    entt::entity player = makeFPSPlayer(w.reg, glm::vec3(0.f, 2.2f, 0.f));
    w.frames(240);

    auto& legs = w.reg.get<PlayerLegs>(player);
    float eyes = w.position(player).y;
    MESSAGE("eyes at " << eyes << " m, speed " << glm::length(w.velocity(player)) << " m/s");
    CHECK(legs.standing);
    // stand_force x stand_speed x 2 x error x mass / 10 = mass x g: 9.8 cm under the height
    CHECK(eyes == doctest::Approx(1.8f - 0.0981f).epsilon(0.01));
    CHECK(glm::length(w.velocity(player)) < 0.01f);
    // The body never turns
    CHECK(angleBetween(w.rotation(player), glm::quat(1.f, 0.f, 0.f, 0.f)) < 1e-4f);
    // The desktop camera is its eyes
    CHECK(w.reg.ctx().get<Camera>().position.y == doctest::Approx(eyes).epsilon(0.01));

}

TEST_CASE("PLAYER.2 walking holds the walk speed along the view, running the sprint speed, and letting go stops") {

    PlayerWorld w;
    entt::entity player = makeFPSPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    w.frames(120);

    auto& input = w.input(player);
    input.move = glm::vec2(0.f, 1.f);
    w.frames(120);
    glm::vec3 v = w.velocity(player);
    MESSAGE("walk " << v.x << " " << v.y << " " << v.z);
    CHECK(v.z == doctest::Approx(-2.f).epsilon(0.03));
    CHECK(std::abs(v.x) < 0.02f);
    CHECK(w.reg.get<PlayerLegs>(player).walking);

    // Turned a quarter to the left: forward is -X
    w.reg.get<PlayerView>(player).yaw = glm::radians(90.f);
    input.run = true;
    w.frames(120);
    v = w.velocity(player);
    MESSAGE("run " << v.x << " " << v.y << " " << v.z);
    CHECK(v.x == doctest::Approx(-4.f).epsilon(0.03));
    CHECK(std::abs(v.z) < 0.05f);

    input.move = glm::vec2(0.f);
    w.frames(60);
    CHECK(glm::length(w.velocity(player)) < 0.02f);

}

TEST_CASE("PLAYER.3 a jump leaves the ground and comes back to stand") {

    PlayerWorld w;
    entt::entity player = makeFPSPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    w.frames(120);
    float rest = w.position(player).y;

    w.input(player).jump = true;
    float top = rest;
    for(int i = 0; i < 20; i++) {
        w.frames(1);
        top = std::max(top, w.position(player).y);
    }
    w.input(player).jump = false;
    bool flew = false;
    for(int i = 0; i < 90; i++) {
        w.frames(1);
        top = std::max(top, w.position(player).y);
        flew = flew || !w.reg.get<PlayerLegs>(player).standing;
    }
    MESSAGE("jump of " << top - rest << " m");
    CHECK(top - rest > 0.6f);
    CHECK(flew);
    w.frames(180);
    CHECK(w.position(player).y == doctest::Approx(rest).epsilon(0.01));

}

TEST_CASE("PLAYER.4 a hand follows its controller in position and rotation, and the body stays where it stands") {

    PlayerWorld w;
    entt::entity player = makeVRPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    track(w, player);
    w.frames(120);

    entt::entity hand = w.reg.get<VRBody>(player).hands[VRInput::right];
    auto& tracking = w.reg.get<VRTracking>(player);
    glm::vec3 body = w.position(player);

    // Where the controller is from the head is where the hand is from the body
    glm::vec3 offset = tracking.hands[VRInput::right].pose.position - tracking.head.position;
    CHECK(glm::length(w.position(hand) - (body + offset)) < 0.01f);
    CHECK(w.reg.get<BodyDrive>(hand).driven);

    // The controller moves 30 cm up and to the side in a tenth of a second and turns
    glm::quat turned = glm::angleAxis(glm::radians(70.f), glm::normalize(glm::vec3(0.3f, 1.f, 0.2f)));
    for(int i = 1; i <= 6; i++) {
        tracking.hands[VRInput::right].pose.position += glm::vec3(0.05f, 0.05f, 0.f);
        tracking.hands[VRInput::right].velocity = glm::vec3(3.f, 3.f, 0.f);
        tracking.hands[VRInput::right].pose.rotation = glm::slerp(glm::quat(1.f, 0.f, 0.f, 0.f), turned, i / 6.f);
        w.frames(1);
    }
    tracking.hands[VRInput::right].velocity = glm::vec3(0.f);
    offset = tracking.hands[VRInput::right].pose.position - tracking.head.position;
    float lag = glm::length(w.position(hand) - (w.position(player) + offset));
    MESSAGE("lag at the end of the move " << lag * 100.f << " cm");
    // 4.2 m/s: the feed forward fades with the error, the rest closes at 0.607 a step
    CHECK(lag < 0.1f);

    w.frames(30);
    float error = glm::length(w.position(hand) - (w.position(player) + offset));
    float angle = angleBetween(w.rotation(hand), turned);
    MESSAGE("at rest " << error * 1000.f << " mm, " << glm::degrees(angle) << " degrees");
    CHECK(error < 0.005f);
    CHECK(angle < glm::radians(1.f));
    CHECK(w.reg.get<Hand>(hand).teleports == 0);
    // 1.5 kg against 70
    CHECK(glm::length(glm::vec2(w.position(player).x - body.x, w.position(player).z - body.z)) < 0.03f);

}

TEST_CASE("PLAYER.5 a hand more than a metre from its controller is put there") {

    PlayerWorld w;
    entt::entity player = makeVRPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    track(w, player);
    w.frames(60);

    entt::entity hand = w.reg.get<VRBody>(player).hands[VRInput::left];
    auto& tracking = w.reg.get<VRTracking>(player);
    tracking.hands[VRInput::left].pose.position += glm::vec3(0.f, 0.f, -1.6f);
    w.frames(2);
    CHECK(w.reg.get<Hand>(hand).teleports == 1);
    glm::vec3 offset = tracking.hands[VRInput::left].pose.position - tracking.head.position;
    CHECK(glm::length(w.position(hand) - (w.position(player) + offset)) < 0.02f);

}

TEST_CASE("PLAYER.6 the body follows the head in the room, the play area turns around it, poses are held without focus") {

    PlayerWorld w;
    entt::entity player = makeVRPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    track(w, player);
    w.frames(60);
    glm::vec3 start = w.position(player);
    auto& tracking = w.reg.get<VRTracking>(player);
    auto& vr = w.reg.get<VRBody>(player);

    // A step of 40 cm to the right, hands with the head
    for(int i = 0; i < 20; i++) {
        tracking.head.position.x += 0.02f;
        for(auto& hand : tracking.hands) hand.pose.position.x += 0.02f;
        w.frames(1);
    }
    w.frames(30);
    CHECK(w.position(player).x - start.x == doctest::Approx(0.4f).epsilon(0.02));
    CHECK(std::abs(w.position(player).z - start.z) < 0.01f);

    // The play area is under the head: the rig puts the head on the body
    auto* rig = w.reg.ctx().find<VRRig>();
    if(!rig) rig = &w.reg.ctx().emplace<VRRig>();
    w.frames(1);
    glm::vec3 head_world = glm::vec3(rig->transform() * glm::vec4(tracking.head.position, 1.f));
    CHECK(glm::length(head_world - w.reg.get<Transform>(player).position) < 1e-3f);

    // Crouching 50 cm: the legs hold the body lower
    tracking.head.position.y -= 0.5f;
    for(auto& hand : tracking.hands) hand.pose.position.y -= 0.5f;
    w.frames(120);
    CHECK(w.position(player).y == doctest::Approx(start.y - 0.5f).epsilon(0.02));
    tracking.head.position.y += 0.5f;
    for(auto& hand : tracking.hands) hand.pose.position.y += 0.5f;
    w.frames(120);

    // The right stick turns the play area: the right hand goes round the body
    entt::entity hand = vr.hands[VRInput::right];
    w.input(player).view = glm::vec2(1.f, 0.f);
    w.frames(30);
    w.input(player).view = glm::vec2(0.f);
    w.frames(30);
    CHECK(vr.yaw == doctest::Approx(-3.14159f * 30. * PlayerWorld::dt).epsilon(0.01));
    glm::vec3 offset = vr.rotation() * (tracking.hands[VRInput::right].pose.position - tracking.head.position);
    CHECK(glm::length(w.position(hand) - (w.position(player) + offset)) < 0.01f);
    CHECK(w.reg.get<Hand>(hand).teleports == 0);

    // The system menu: the runtime's poses jump, nothing moves; nor on the way back
    glm::vec3 before = w.position(player), hand_before = w.position(hand);
    tracking.focused = false;
    tracking.head.position += glm::vec3(3.f, 0.f, 2.f);
    for(auto& h : tracking.hands) h.pose.position += glm::vec3(-2.f, 0.f, 5.f);
    w.frames(20);
    CHECK(vr.pose_hold);
    CHECK(glm::length(w.position(player) - before) < 0.01f);
    CHECK(glm::length(w.position(hand) - hand_before) < 0.02f);
    tracking.focused = true;
    for(auto& h : tracking.hands) h.pose.position = tracking.head.position + glm::vec3(0.2f, -0.4f, -0.35f);
    tracking.hands[VRInput::left].pose.position.x -= 0.4f;
    w.frames(20);
    CHECK(!vr.pose_hold);
    CHECK(glm::length(w.position(player) - before) < 0.01f);
    CHECK(w.reg.get<Hand>(hand).teleports == 0);

}

TEST_CASE("PLAYER.7 a hand grabs the box it touches while the grip is held, carries it, and throws it with the controller's velocity") {

    PlayerWorld w;
    entt::entity player = makeVRPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    track(w, player);
    auto& tracking = w.reg.get<VRTracking>(player);
    auto& controller = tracking.hands[VRInput::right];
    entt::entity hand = w.reg.get<VRBody>(player).hands[VRInput::right];

    // A 2 kg box on a table of its own in front of the player (a static box)
    entt::entity table = w.reg.create();
    w.reg.emplace<Transform>(table, glm::vec3(0.3f, 0.5f, -0.5f), glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(1.f));
    auto& table_body = w.reg.emplace<RigidBody>(table);
    table_body.motion = RigidBody::Motion::Static;
    table_body.half_extent = glm::vec3(0.3f, 0.5f, 0.3f);
    w.reg.emplace<Ungrabbable>(table);
    entt::entity box = w.box(glm::vec3(0.3f, 1.11f, -0.5f), 0.2f, 2.f);
    w.frames(60);
    glm::vec3 box_rest = w.position(box);
    CHECK(box_rest.y == doctest::Approx(1.1f).epsilon(0.01));

    // The hand comes down on the box without the grip: it rests on it, nothing is held
    auto moveTo = [&](glm::vec3 to, int frames) {
        glm::vec3 from = controller.pose.position;
        for(int i = 1; i <= frames; i++) {
            controller.pose.position = glm::mix(from, to, i / (float) frames);
            controller.velocity = (to - from) / (float) (frames * PlayerWorld::dt);
            w.frames(1);
        }
        controller.velocity = glm::vec3(0.f);
    };
    glm::vec3 head = tracking.head.position;
    glm::vec3 body = w.position(player);
    // Controller targets are given in the world: stage = world - body + head (yaw 0)
    auto stage = [&](glm::vec3 world) { return world - w.position(player) + head; };
    moveTo(stage(glm::vec3(0.3f, 1.5f, -0.5f)), 30);
    moveTo(stage(glm::vec3(0.3f, 1.2f, -0.5f)), 30);
    w.frames(20);
    auto& grabber = w.reg.get<Grabber>(hand);
    CHECK(!grabber.grabbed);
    CHECK(w.position(hand).y > 1.2f);   // on the box, not in it

    controller.grip = 1.f;
    w.frames(3);
    REQUIRE(grabber.grabbed);
    CHECK((grabber.object == box));
    CHECK(w.reg.all_of<Grabbed>(box));

    // Lifted 40 cm and carried to the side: the box goes with the hand
    glm::vec3 relative = w.position(box) - w.position(hand);
    moveTo(stage(glm::vec3(0.3f, 1.6f, -0.5f)), 30);
    moveTo(stage(glm::vec3(0.8f, 1.6f, -0.5f)), 30);
    w.frames(20);
    CHECK(w.position(box).y > box_rest.y + 0.3f);
    CHECK(glm::length((w.position(box) - w.position(hand)) - relative) < 0.02f);
    MESSAGE("the body moved " << glm::length(w.position(player) - body) * 100.f << " cm carrying 2 kg");

    // Thrown forward and up at 4 m/s
    glm::vec3 throw_velocity(0.f, 2.f, -3.5f);
    for(int i = 0; i < 6; i++) {
        controller.pose.position += throw_velocity * (float) PlayerWorld::dt;
        controller.velocity = throw_velocity;
        w.frames(1);
    }
    controller.grip = 0.f;
    w.frames(1);
    CHECK(!grabber.grabbed);
    CHECK(!w.reg.all_of<Grabbed>(box));
    glm::vec3 v = w.velocity(box);
    MESSAGE("thrown at " << v.x << " " << v.y << " " << v.z);
    CHECK(glm::length(v - (throw_velocity + w.velocity(player))) < 0.5f);
    w.frames(60);
    CHECK(w.position(box).z < -1.5f);

    // The table can not be grabbed, the ground can: the hand stays on it against its controller
    moveTo(stage(glm::vec3(0.5f, 0.3f, 0.1f)), 30);
    moveTo(stage(glm::vec3(0.5f, 0.02f, 0.1f)), 30);
    controller.grip = 1.f;
    w.frames(5);
    REQUIRE(grabber.grabbed);
    CHECK((grabber.object == entt::null));
    glm::vec3 held = w.position(hand);
    moveTo(stage(glm::vec3(0.5f, 0.4f, 0.1f)), 10);
    w.frames(5);
    CHECK(glm::length(w.position(hand) - held) < 0.02f);
    controller.grip = 0.f;
    w.frames(120);
    CHECK(!grabber.grabbed);
    // Free again, it goes back to its controller
    CHECK(glm::length(w.position(hand) - (w.position(player) + controller.pose.position - head)) < 0.02f);

}

TEST_CASE("PLAYER.8 the FPS player picks up the box it looks at, the box follows its view, and is thrown") {

    PlayerWorld w;
    entt::entity player = makeFPSPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    entt::entity box = w.box(glm::vec3(0.f, 0.15f, -1.5f), 0.3f, 5.f);
    w.frames(120);

    auto& input = w.input(player);
    auto& view = w.reg.get<PlayerView>(player);
    auto& holder = w.reg.get<Holder>(player);

    // Looking over it: nothing to pick up
    input.interact = true;
    w.frames(1);
    input.interact = false;
    w.frames(1);
    CHECK((holder.held == entt::null));

    // Looking at it
    glm::vec3 to = w.position(box) - w.position(player);
    view.pitch = std::atan2(to.y, -to.z);
    input.interact = true;
    w.frames(1);
    input.interact = false;
    w.frames(1);
    REQUIRE((holder.held == box));
    CHECK(w.reg.all_of<BodyDrive, Held>(box));

    // Level again: the box comes in front of the eyes
    view.pitch = 0.f;
    w.frames(90);
    glm::vec3 place = w.position(player) + view.forward() * holder.distance;
    MESSAGE("held " << glm::length(w.position(box) - place) * 100.f << " cm from its place, eyes at " << w.position(player).y);
    CHECK(glm::length(w.position(box) - place) < 0.03f);

    // A quarter turn: it goes round with the view, and turns with it
    glm::quat before = w.rotation(box);
    for(int i = 1; i <= 45; i++) {
        view.yaw = glm::radians(2.f * i);
        w.frames(1);
    }
    w.frames(60);
    place = w.position(player) + view.forward() * holder.distance;
    CHECK(glm::length(w.position(box) - place) < 0.03f);
    CHECK(angleBetween(w.rotation(box), glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f)) * before) < glm::radians(3.f));

    // The wheel pushes it away
    input.control = 2.f;
    w.frames(1);
    input.control = 0.f;
    CHECK(holder.distance == doctest::Approx(1.5f));

    // Thrown along the view
    input.primary = true;
    w.frames(1);
    input.primary = false;
    w.frames(1);
    CHECK((holder.held == entt::null));
    CHECK(!w.reg.any_of<BodyDrive, Held>(box));
    CHECK(w.velocity(box).x < -6.f);

    // Picked up again and destroyed in the hand: the hold lets go
    w.frames(120);
    to = w.position(box) - w.position(player);
    view.yaw = std::atan2(-to.x, -to.z);
    view.pitch = std::asin(to.y / glm::length(to));
    if(glm::length(to) < holder.reach) {
        input.interact = true;
        w.frames(1);
        input.interact = false;
        w.frames(1);
        REQUIRE((holder.held == box));
        w.frames(10);
        w.reg.destroy(box);
        w.frames(10);
        CHECK((holder.held == entt::null));
    }

}

TEST_CASE("PLAYER.9 what is too heavy for the hold stays on the ground, and a hand does not go through a wall") {

    PlayerWorld w;
    entt::entity player = makeFPSPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    entt::entity heavy = w.box(glm::vec3(0.f, 0.3f, -1.5f), 0.6f, 400.f);
    w.frames(120);

    auto& input = w.input(player);
    auto& view = w.reg.get<PlayerView>(player);
    glm::vec3 to = w.position(heavy) - w.position(player);
    view.pitch = std::atan2(to.y, -to.z);
    input.interact = true;
    w.frames(1);
    input.interact = false;
    w.frames(1);
    REQUIRE((w.reg.get<Holder>(player).held == heavy));
    view.pitch = 0.f;
    w.frames(60);
    // 1500 N against 3924 N of weight
    CHECK(w.position(heavy).y < 0.35f);

    PlayerWorld v;
    entt::entity vr_player = makeVRPlayer(v.reg, glm::vec3(0.f, 1.8f, 0.f));
    track(v, vr_player);
    entt::entity wall = v.reg.create();
    v.reg.emplace<Transform>(wall, glm::vec3(0.f, 1.5f, -0.8f), glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(1.f));
    auto& wall_body = v.reg.emplace<RigidBody>(wall);
    wall_body.motion = RigidBody::Motion::Static;
    wall_body.half_extent = glm::vec3(2.f, 1.5f, 0.1f);
    v.frames(60);
    entt::entity hand = v.reg.get<VRBody>(vr_player).hands[VRInput::right];
    auto& controller = v.reg.get<VRTracking>(vr_player).hands[VRInput::right];
    // The controller goes 30 cm past the face of the wall (at z = -0.7)
    for(int i = 0; i < 30; i++) {
        controller.pose.position.z -= 0.02f;
        v.frames(1);
    }
    v.frames(30);
    MESSAGE("hand at z " << v.position(hand).z << ", body pushed back to z " << v.position(vr_player).z);
    CHECK(v.position(hand).z > -0.7f);
    // The drive pushes the body away from the wall instead (Unity: the joint's reaction)
    CHECK(v.position(vr_player).z > 0.05f);
    CHECK(v.reg.get<Hand>(hand).teleports == 0);

}

}
