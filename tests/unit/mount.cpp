#include <doctest/doctest.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <cmath>
#include <memory>
#include <vector>

#include "logic/animals/steer.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/player/player.h"
#include "logic/player/player_systems.h"
#include "physics/physics_sys.h"
#include "physics/physics_world.h"

using namespace dy;

namespace {

// A flat ground, an FPS player and something to ride that is no animal of the library: a Transform with a
// Mountable, which SteeredWalk walks over the plane. The systems in the order of scene player_horse
struct RidingWorld {
    entt::registry reg;
    std::vector<std::unique_ptr<System>> systems;
    static constexpr double dt = 1. / 60. + 1e-7;

    RidingWorld() {
        reg.emplace<Ground>(reg.create());
        systems.push_back(std::make_unique<MountablesSys>(reg, nlohmann::json::object()));
        systems.push_back(std::make_unique<LookTurnSys>(reg));
        systems.push_back(std::make_unique<MountingSys>(reg));
        systems.push_back(std::make_unique<MountSteeringSys>(reg));
        systems.push_back(std::make_unique<SteeredWalkSys>(reg, nlohmann::json::object()));
        systems.push_back(std::make_unique<PlayerLegsSys>(reg));
        systems.push_back(std::make_unique<RiderSeatSys>(reg));
        systems.push_back(std::make_unique<HoldSys>(reg));
        systems.push_back(std::make_unique<BodyDrivesSys>(reg));
        systems.push_back(std::make_unique<RigidBodySys>(reg));
        systems.push_back(std::make_unique<PhysicsSys>(reg));
        systems.push_back(std::make_unique<PlayerViewpointSys>(reg));
        for(auto& s : systems) s->preinit();
        for(auto& s : systems) s->init();
    }

    ~RidingWorld() {
        for(auto& s : systems) s->finish();
        systems.clear();
    }

    void frames(int n) {
        for(int i = 0; i < n; i++) {
            for(auto& s : systems) s->tick(dt);
        }
    }

    ScriptedInput& input(entt::entity e) { return reg.get_or_emplace<ScriptedInput>(e); }

    JPH::BodyInterface& bodies() { return reg.ctx().get<PhysicsWorld>().bodies(); }
    JPH::BodyID id(entt::entity e) { return JPH::BodyID(reg.get<RigidBody>(e).body); }
    glm::vec3 position(entt::entity e) { return toGlm(JPH::Vec3(bodies().GetPosition(id(e)))); }
    glm::vec3 velocity(entt::entity e) { return toGlm(bodies().GetLinearVelocity(id(e))); }

    // A horse's size: back 1.45 m up, heading -z, its origin on the ground at `at`
    entt::entity horse(glm::vec3 at) {
        entt::entity e = reg.create();
        reg.emplace<Transform>(e, at);
        auto& m = reg.emplace<Mountable>(e);
        m.saddle = glm::vec3(0.f, 1.45f, 0.f);
        m.centre = glm::vec3(0.f, 1.1f, 0.f);
        m.half_extent = glm::vec3(0.3f, 0.35f, 0.9f);
        m.walk_speed = 1.6f;
        m.run_speed = 6.f;
        return e;
    }

    // Presses jump for a frame
    void jump(entt::entity player) {
        input(player).jump = true;
        frames(2);
        input(player).jump = false;
        frames(1);
    }
};

float wrapAngle(float a) {
    while(a > 3.14159265f) a -= 6.2831853f;
    while(a < -3.14159265f) a += 6.2831853f;
    return a;
}

}

TEST_SUITE("mount") {

TEST_CASE("MOUNT.1 the input components answer for their devices, and the device used last speaks for the player") {

    entt::registry reg;
    entt::entity player = reg.create();
    CHECK(!playerInput(reg, player).isActive());
    CHECK(playerInput(reg, player).getMovementAxis() == glm::vec2(0.f));

    auto& keys = reg.emplace<MouseKeyboardInput>(player);
    keys.keys.set(MouseKeyboardInput::W);
    keys.keys.set(MouseKeyboardInput::D);
    glm::vec2 move = keys.getMovementAxis();
    CHECK(move.x == doctest::Approx(std::sqrt(0.5f)));
    CHECK(move.y == doctest::Approx(std::sqrt(0.5f)));
    keys.keys.set(MouseKeyboardInput::S);
    CHECK(keys.getMovementAxis() == glm::vec2(1.f, 0.f));
    keys.keys.set(MouseKeyboardInput::Space);
    keys.keys.set(MouseKeyboardInput::LeftShift);
    CHECK(keys.getJumpButton());
    CHECK(keys.getRunButton());
    CHECK(!keys.getInteractButton());
    // The mouse: counts to radians, up is up; nothing while the menus have it
    keys.mouse = glm::vec2(100.f, 40.f);
    keys.left = true;
    CHECK(keys.viewIsDelta());
    CHECK(keys.getViewAxis().x == doctest::Approx(0.25f));
    CHECK(keys.getViewAxis().y == doctest::Approx(-0.1f));
    CHECK(keys.getPrimaryActionButton());
    keys.captured = false;
    CHECK(keys.getViewAxis() == glm::vec2(0.f));
    CHECK(!keys.getPrimaryActionButton());

    // A gamepad: nothing inside the dead zone or without one, full at the rim
    auto& pad = reg.emplace<ControllersInput>(player);
    pad.left_stick = glm::vec2(0.f, 1.f);
    pad.a = true;
    CHECK(!pad.isActive());
    CHECK(pad.getMovementAxis() == glm::vec2(0.f));
    CHECK(!pad.getJumpButton());
    pad.connected = true;
    CHECK(pad.getMovementAxis().y == doctest::Approx(1.f));
    CHECK(pad.getJumpButton());
    pad.left_stick = glm::vec2(0.1f, 0.1f);
    CHECK(pad.getMovementAxis() == glm::vec2(0.f));
    pad.left_stick = glm::vec2(0.6f, 0.f);
    CHECK(pad.getMovementAxis().x == doctest::Approx(0.5f));
    CHECK(!pad.viewIsDelta());

    // A headset's controllers: past half their travel, with the session's focus
    VRPlayerInput vr;
    vr.left_stick = glm::vec2(0.f, 0.8f);
    CHECK(vr.getMovementAxis() == glm::vec2(0.f));
    vr.focused = true;
    CHECK(vr.getMovementAxis() == glm::vec2(0.f, 1.f));
    vr.left_stick = glm::vec2(0.f, 0.4f);
    CHECK(vr.getMovementAxis() == glm::vec2(0.f));

    // Who speaks: the one used last, a script before any device
    auto& keys2 = reg.get<MouseKeyboardInput>(player);
    auto& pad2 = reg.get<ControllersInput>(player);
    keys2.last_action = 1.;
    pad2.last_action = 2.;
    CHECK(&playerInput(reg, player) == static_cast<const GenericPlayerInput*>(&pad2));
    keys2.last_action = 3.;
    CHECK(&playerInput(reg, player) == static_cast<const GenericPlayerInput*>(&keys2));
    pad2.connected = false;
    pad2.last_action = 4.;
    CHECK(&playerInput(reg, player) == static_cast<const GenericPlayerInput*>(&keys2));
    auto& script = reg.emplace<ScriptedInput>(player);
    CHECK(&playerInput(reg, player) == static_cast<const GenericPlayerInput*>(&script));

}

TEST_CASE("MOUNT.2 jump next to an animal gets on it: carried up to the saddle without a jolt; away from one it is a jump") {

    RidingWorld w;
    entt::entity player = makeFPSPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    // Its flank 15 cm from the player's side
    entt::entity horse = w.horse(glm::vec3(0.108f + 0.15f + 0.3f, 0.f, 0.f));
    w.frames(120);
    auto& rider = w.reg.get<Rider>(player);
    auto& legs = w.reg.get<PlayerLegs>(player);
    REQUIRE((w.reg.get<Mountable>(horse).collider != entt::null));
    CHECK((rider.mount == entt::null));
    CHECK(legs.standing);

    w.input(player).jump = true;
    w.frames(1);
    CHECK((rider.mount == horse));
    CHECK(legs.limp);
    CHECK(w.reg.all_of<AnimalSteer>(horse));
    CHECK((w.reg.get<Mountable>(horse).rider == player));

    // Up and over: the velocity never changes by more than the seat's cap allows in a step
    glm::vec3 last = w.velocity(player);
    float worst = 0.f;
    for(int i = 0; i < 180; i++) {
        if(i == 5) w.input(player).jump = false;
        w.frames(1);
        glm::vec3 v = w.velocity(player);
        worst = std::max(worst, glm::length(v - last) * 60.f);
        last = v;
    }
    glm::vec3 seat = saddleOf(w.reg, horse) + glm::vec3(0.f, rider.seat, 0.f);
    glm::vec3 eyes = w.position(player);
    MESSAGE("eyes " << eyes.x << " " << eyes.y << " " << eyes.z << ", seat " << seat.x << " " << seat.y << " " << seat.z << ", largest acceleration "
        << worst << " m/s2");
    CHECK((rider.mount == horse));
    CHECK(glm::distance(eyes, seat) < 0.02f);
    CHECK(glm::length(w.velocity(player)) < 0.02f);
    CHECK(worst < rider.max_acceleration * 1.05f);
    CHECK(rider.blend == doctest::Approx(1.f));

    // Away from any animal: a jump
    RidingWorld far;
    entt::entity walker = makeFPSPlayer(far.reg, glm::vec3(0.f, 1.8f, 0.f));
    far.horse(glm::vec3(3.f, 0.f, 0.f));
    far.frames(120);
    float rest = far.position(walker).y, top = rest;
    far.input(walker).jump = true;
    for(int i = 0; i < 20; i++) {
        far.frames(1);
        top = std::max(top, far.position(walker).y);
    }
    CHECK((far.reg.get<Rider>(walker).mount == entt::null));
    CHECK(top > rest + 0.2f);

}

TEST_CASE("MOUNT.3 the walk axis along the view steers the animal, run asks for its run, and the rider stays on the saddle") {

    RidingWorld w;
    entt::entity player = makeFPSPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    entt::entity horse = w.horse(glm::vec3(0.55f, 0.f, 0.f));
    w.frames(120);
    w.jump(player);
    auto& rider = w.reg.get<Rider>(player);
    REQUIRE((rider.mount == horse));
    w.frames(120);

    // Nothing asked: it stands
    CHECK(glm::distance(w.reg.get<Transform>(horse).position, glm::vec3(0.55f, 0.f, 0.f)) < 0.01f);

    // Forward, looking a quarter to the left: toward -x at the walk's speed
    auto& input = w.input(player);
    w.reg.get<PlayerView>(player).yaw = glm::radians(90.f);
    input.move = glm::vec2(0.f, 1.f);
    float off = 0.f, jerk = 0.f, last_speed = 0.f, last_change = 0.f;
    for(int i = 0; i < 480; i++) {
        glm::vec3 before = w.reg.get<Transform>(horse).position;
        w.frames(1);
        float speed = glm::distance(w.reg.get<Transform>(horse).position, before) * 60.f;
        float change = (speed - last_speed) * 60.f;
        if(i > 0) jerk = std::max(jerk, std::abs(change - last_change));
        last_speed = speed;
        last_change = change;
        if(i > 60) off = std::max(off, glm::distance(w.position(player), saddleOf(w.reg, horse) + glm::vec3(0.f, rider.seat, 0.f)));
    }
    float heading = headingOf(w.reg.get<Transform>(horse).rotation);
    MESSAGE("heading " << glm::degrees(heading) << " deg, speed " << last_speed << " m/s, rider at most " << off
        << " m off its seat, the animal's acceleration changed by at most " << jerk << " m/s2 a frame");
    CHECK(std::abs(wrapAngle(heading - glm::radians(90.f))) < glm::radians(2.f));
    CHECK(last_speed == doctest::Approx(1.6f).epsilon(0.03));
    CHECK(w.velocity(player).x == doctest::Approx(-1.6f).epsilon(0.05));
    CHECK(off < 0.12f);
    // The animal's speed is eased, and its change too
    CHECK(jerk < 0.5f);

    // Run, and to the right of the view: toward -z
    input.run = true;
    input.move = glm::vec2(1.f, 0.f);
    off = 0.f;
    for(int i = 0; i < 480; i++) {
        w.frames(1);
        if(i > 60) off = std::max(off, glm::distance(w.position(player), saddleOf(w.reg, horse) + glm::vec3(0.f, rider.seat, 0.f)));
    }
    heading = headingOf(w.reg.get<Transform>(horse).rotation);
    glm::vec3 v = w.velocity(player);
    MESSAGE("run: heading " << glm::degrees(heading) << " deg, rider " << v.x << " " << v.y << " " << v.z << " m/s, at most " << off << " m off its seat");
    CHECK(std::abs(wrapAngle(heading)) < glm::radians(2.f));
    CHECK(v.z == doctest::Approx(-6.f).epsilon(0.05));
    CHECK(off < 0.3f);

    // Let go: it comes to a halt, the rider with it
    input.move = glm::vec2(0.f);
    w.frames(360);
    CHECK(glm::length(w.velocity(player)) < 0.05f);
    CHECK((rider.mount == horse));
    CHECK(glm::distance(w.position(player), saddleOf(w.reg, horse) + glm::vec3(0.f, rider.seat, 0.f)) < 0.03f);

}

TEST_CASE("MOUNT.4 jump on the animal gets off: on its legs beside it, the animal no longer steered, and bumped into again") {

    RidingWorld w;
    entt::entity player = makeFPSPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    entt::entity horse = w.horse(glm::vec3(0.55f, 0.f, 0.f));
    w.frames(120);
    w.jump(player);
    auto& rider = w.reg.get<Rider>(player);
    auto& legs = w.reg.get<PlayerLegs>(player);
    REQUIRE((rider.mount == horse));
    w.input(player).move = glm::vec2(0.f, 1.f);
    w.frames(240);
    w.input(player).move = glm::vec2(0.f);
    w.frames(300);
    // Halted, and still its rider's
    CHECK(w.reg.all_of<SteeredWalk>(horse));

    w.input(player).jump = true;
    w.frames(1);
    CHECK((rider.mount == entt::null));
    CHECK(!legs.limp);
    CHECK(!w.reg.all_of<AnimalSteer>(horse));
    CHECK((w.reg.get<Mountable>(horse).rider == entt::null));
    // Held down, the button neither puts it back on nor makes it jump when it lands
    w.frames(240);
    w.input(player).jump = false;
    w.frames(60);
    CHECK((rider.mount == entt::null));
    CHECK(legs.standing);
    glm::vec3 eyes = w.position(player), at = w.reg.get<Transform>(horse).position;
    MESSAGE("player " << eyes.x << " " << eyes.y << " " << eyes.z << ", animal " << at.x << " " << at.z);
    CHECK(eyes.y == doctest::Approx(1.8f - 0.0981f).epsilon(0.02));
    // To its left (it heads -z: toward -x), clear of its box
    CHECK(eyes.x < at.x - 0.3f - 0.108f + 0.02f);
    // The animal has stopped and is nobody's
    CHECK(!w.reg.all_of<SteeredWalk>(horse));

    // Walking into it: stopped by its body
    w.reg.get<PlayerView>(player).yaw = glm::radians(-90.f);     // looks toward +x
    w.input(player).move = glm::vec2(0.f, 1.f);
    w.frames(240);
    eyes = w.position(player);
    CHECK(eyes.x < at.x - 0.3f);
    CHECK((rider.mount == entt::null));

}

TEST_CASE("MOUNT.5 an animal that goes away under its rider leaves it on its legs") {

    RidingWorld w;
    entt::entity player = makeFPSPlayer(w.reg, glm::vec3(0.f, 1.8f, 0.f));
    entt::entity horse = w.horse(glm::vec3(0.55f, 0.f, 0.f));
    w.frames(120);
    w.jump(player);
    auto& rider = w.reg.get<Rider>(player);
    REQUIRE((rider.mount == horse));
    w.frames(120);

    // Put somewhere else (a physical animal whose episode starts again): the seat is lost
    w.reg.get<Transform>(horse).position += glm::vec3(30.f, 0.f, 0.f);
    w.frames(5);
    CHECK((rider.mount == entt::null));
    CHECK(!w.reg.all_of<AnimalSteer>(horse));

    // Destroyed: its collider goes with it, nothing is left of the ride
    entt::entity other = w.horse(glm::vec3(w.position(player).x + 0.55f, 0.f, w.position(player).z));
    w.frames(120);
    w.jump(player);
    REQUIRE((rider.mount == other));
    entt::entity collider = w.reg.get<Mountable>(other).collider;
    w.reg.destroy(other);
    w.frames(120);
    CHECK((rider.mount == entt::null));
    CHECK(!w.reg.valid(collider));
    CHECK(w.reg.get<PlayerLegs>(player).standing);

}

}
