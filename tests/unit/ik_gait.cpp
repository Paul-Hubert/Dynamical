#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "logic/agents/agent_io.h"
#include "logic/agents/gait.h"
#include "logic/agents/ground_field.h"
#include "logic/agents/humanbot.h"
#include "logic/agents/ik.h"
#include "logic/agents/rig_pose.h"
#include "logic/components/transform.h"
#include "logic/agents/sphere_roller.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "physics/physics_world.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

namespace {

const float deg = 180.f / 3.14159265358979f;
const char* const body_file = "resources/agents/bodies/heidelbot_notoes.body.json";

bool haveBody() {
    return std::filesystem::exists(body_file);
}

using Vars = std::vector<std::pair<std::string, std::string>>;

struct Walk {
    GaitReport report;
    GaitState state;
    BalanceState balance;
    uint64_t hash = 0;
};

// The walker of a scene after `seconds`, `each` called before every tick
Walk walk(const std::string& scene, Vars vars, double seconds, std::function<void(World&, int)> each = {}) {
    ScenarioRunner runner(scene, vars, SceneLoadOptions {});
    auto& world = runner.world(0);
    const int ticks = (int) std::lround(seconds * 60.);
    for(int t = 0; t < ticks; t++) {
        if(each) each(world, t);
        world.tick();
    }
    Walk w;
    auto view = world.reg.view<GaitReport, GaitState, BalanceState>();
    REQUIRE(view.begin() != view.end());
    entt::entity e = *view.begin();
    w.report = world.reg.get<GaitReport>(e);
    w.state = world.reg.get<GaitState>(e);
    w.balance = world.reg.get<BalanceState>(e);
    const auto& bot = world.reg.ctx().get<HumanBot>();
    for(size_t i = 0; i < bot.bodies.size(); i++) {
        for(int k = 0; k < 3; k++) {
            uint32_t bits;
            std::memcpy(&bits, &bot.state.position[i][k], 4);
            w.hash = (w.hash ^ bits) * 1099511628211ull;
        }
    }
    return w;
}

std::string describe(const GaitReport& r) {
    char text[640];
    std::snprintf(text, sizeof(text),
        "steps %d, speed %.3f m/s | contact %.2f mm, penetration %.2f mm, slip %.2f mm, sole %.2f deg | stance ik %.2f mm %.2f deg (swing %.1f mm %.1f deg), limits took %.2f deg, heel relief %.1f deg | "
        "over IK limits %.2f deg, over body limits %.2f deg (joint %d) | capture %.1f mm, com error %.2f mm, outside %.1f mm, lateral margin %.1f mm, gap %.1f mm",
        r.steps, r.mean_speed, r.max_contact_error * 1e3f, r.max_penetration * 1e3f, r.max_slip * 1e3f, r.max_sole_angle * deg,
        r.max_ik_position * 1e3f, r.max_ik_rotation * deg, r.max_swing_ik_position * 1e3f, r.max_swing_ik_rotation * deg, r.max_ik_limit * deg, r.max_relief * deg, r.max_limit_excess * deg, r.max_body_limit_excess * deg, r.worst_joint,
        r.max_capture_error * 1e3f, r.max_com_error * 1e3f, r.max_outside * 1e3f, r.min_lateral_margin * 1e3f, r.max_gap * 1e3f);
    return text;
}

// The thresholds of docs/physical-agents/ik-gait.md
// over_limits: degrees a planted foot may take its leg past the joint limits (they give way, GaitParams::yield_limits)
void checkWalk(const GaitReport& r, float speed, float over_limits = 0.1f) {
    CHECK(r.max_contact_error < 5e-3f);
    CHECK(r.max_penetration < 5e-3f);
    CHECK(r.max_slip < 2e-3f);
    CHECK(r.max_sole_angle * deg < 1.f);
    CHECK(r.max_limit_excess * deg < over_limits);
    CHECK(r.max_ik_position < 2e-3f);
    CHECK(r.max_com_error < 1e-3f);
    CHECK(r.max_outside < 1e-3f);
    CHECK(r.min_lateral_margin > 0.f);
    CHECK(r.mean_speed == doctest::Approx(speed).epsilon(0.03));
}

// The speed the walker keeps on a slope of these angles, at the default slowdown
float slopeSpeed(float speed, float pitch, float roll) {
    PlaneField plane(pitch, roll);
    glm::vec3 n = plane.normal();
    return speed * std::exp(-GaitParams().slope_slowdown * std::hypot(n.x, n.z) / n.y);
}

}

TEST_SUITE("unit") {

TEST_CASE("IK.1 FABRIK on two bones gives the law of cosines' knee on the pole's side") {
    std::mt19937 gen(5);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    const float a = 0.4515f, b = 0.4056f;
    float worst = 0.f;
    int most = 0;
    for(int n = 0; n < 2000; n++) {
        glm::vec3 hip(u(gen), 1.f + u(gen), u(gen));
        glm::vec3 dir = glm::normalize(glm::vec3(u(gen), -1.5f + u(gen), u(gen)));
        float reach = glm::mix(std::abs(a - b) + 0.05f, 0.99f * (a + b), 0.5f * (u(gen) + 1.f));     // the gait stretches a leg to 0.985
        glm::vec3 target = hip + dir * reach;
        glm::vec3 pole = glm::normalize(glm::vec3(u(gen), 0.2f * u(gen), -1.f));
        if(glm::length(glm::cross(pole, dir)) < 0.2f) continue;

        std::vector<glm::vec3> points = { hip, hip + glm::vec3(0.f, -a, 0.f), hip + glm::vec3(0.f, -a - b, 0.f) };
        FabrikResult result = fabrik(points, { a, b }, target, &pole);
        most = std::max(most, result.iterations);

        float cos_hip = (a * a + reach * reach - b * b) / (2.f * a * reach);
        glm::vec3 side = glm::normalize(pole - dir * glm::dot(pole, dir));
        glm::vec3 knee = hip + dir * (a * cos_hip) + side * (a * std::sqrt(std::max(0.f, 1.f - cos_hip * cos_hip)));
        worst = std::max(worst, glm::length(points[1] - knee));
        CHECK(glm::length(points[2] - target) < 2e-5f);
        CHECK(glm::length(points[1] - points[0]) == doctest::Approx(a).epsilon(1e-4));
    }
    MESSAGE("worst knee distance to the analytic solution ", worst * 1e3f, " mm, most iterations ", most);
    CHECK(worst < 2e-4f);
}

TEST_CASE("IK.2 FABRIK out of reach stretches the chain at the target") {
    std::vector<glm::vec3> points = { glm::vec3(0.f), glm::vec3(0.f, -1.f, 0.f), glm::vec3(0.f, -2.f, 0.f), glm::vec3(0.f, -3.f, 0.f) };
    FabrikResult result = fabrik(points, { 1.f, 1.f, 1.f }, glm::vec3(5.f, 0.f, 0.f));
    CHECK(result.error == doctest::Approx(2.f));
    CHECK(glm::length(points[3] - glm::vec3(3.f, 0.f, 0.f)) < 1e-5f);
}

TEST_CASE("IK.3 FABRIK on a longer chain reaches and keeps its lengths") {
    std::vector<glm::vec3> points = { glm::vec3(0.f), glm::vec3(0.f, 0.3f, 0.f), glm::vec3(0.f, 0.6f, 0.f), glm::vec3(0.f, 0.8f, 0.f), glm::vec3(0.f, 1.f, 0.f) };
    std::vector<float> lengths = { 0.3f, 0.3f, 0.2f, 0.2f };
    glm::vec3 target(0.4f, 0.5f, -0.3f);
    FabrikResult result = fabrik(points, lengths, target);
    CHECK(result.error < 1e-5f);
    for(int i = 0; i < 4; i++) CHECK(glm::length(points[i + 1] - points[i]) == doctest::Approx(lengths[i]).epsilon(1e-4));
}

TEST_CASE("IK.4 joint limits: a rotation inside stays, one outside comes back to the limit") {
    JointLimit knee { glm::radians(2.f), glm::radians(132.f), glm::radians(15.f), glm::radians(13.f) };
    float excess = 0.f;
    glm::quat inside = glm::angleAxis(glm::radians(40.f), glm::vec3(1.f, 0.f, 0.f));
    glm::quat q = RigModel::clampTo(knee, inside, &excess);
    CHECK(excess == 0.f);
    CHECK(std::abs(glm::dot(q, inside)) > 0.999999f);

    q = RigModel::clampTo(knee, glm::angleAxis(glm::radians(-20.f), glm::vec3(1.f, 0.f, 0.f)), &excess);
    CHECK(swingTwist(q).twist * deg == doctest::Approx(2.f).epsilon(1e-3));
    CHECK(excess * deg == doctest::Approx(22.f).epsilon(1e-2));

    // A swing of 40 degrees about y against a 15 degree half cone
    q = RigModel::clampTo(knee, glm::angleAxis(glm::radians(40.f), glm::vec3(0.f, 1.f, 0.f)) * inside, &excess);
    SwingTwist st = swingTwist(q);
    CHECK(2.f * std::asin(st.swing.y) * deg == doctest::Approx(15.f).epsilon(1e-3));
    CHECK(st.twist * deg == doctest::Approx(40.f).epsilon(1e-3));
    CHECK(RigModel::excess(knee, q) == 0.f);
}

TEST_CASE("IK.5 forward kinematics of the rest and spawn poses give the body file's segments") {
    if(!haveBody()) return;
    RigModel model(loadBody(body_file));
    SegmentPoses poses;
    for(bool spawn : { false, true }) {
        model.forward(spawn ? model.spawnPose() : model.restPose(), poses);
        float worst = 0.f, turn = 0.f;
        for(int i = 0; i < model.segments; i++) {
            const SegmentDef& s = model.body->segments[i];
            worst = std::max(worst, glm::length(poses.position[i] - (spawn ? s.spawn_position : s.rest_position)));
            turn = std::max(turn, 1.f - std::abs(glm::dot(poses.rotation[i], spawn ? s.spawn_rotation : s.rest_rotation)));
        }
        CHECK(worst < 1e-5f);
        CHECK(turn < 1e-6f);
    }
    // The centre of mass of the rest pose is over the feet, near the navel
    model.forward(model.restPose(), poses);
    glm::vec3 com = model.centerOfMass(poses);
    MESSAGE("rest centre of mass ", com.x, " ", com.y, " ", com.z);
    CHECK(std::abs(com.x) < 0.01f);
    CHECK(com.y > 0.95f);
    CHECK(com.y < 1.15f);
}

TEST_CASE("IK.6 a leg chain puts the foot on its target, the knee forward, inside the limits") {
    if(!haveBody()) return;
    RigModel model(loadBody(body_file));
    FootGeometry foot = footGeometry(model, false);
    CHECK(foot.half_length > foot.half_width);
    CHECK(foot.half_length == doctest::Approx(0.139f).epsilon(0.05));
    MESSAGE("sole ", 2.f * foot.half_width, " x ", 2.f * foot.half_length, " m, toe out ", foot.toe_out * deg, " deg");

    std::mt19937 gen(11);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    SegmentPoses rest;
    model.forward(model.restPose(), rest);
    const glm::vec3 ankle = rest.position[foot.segment];
    float worst = 0.f, worst_turn = 0.f;
    int clamped = 0;
    for(int n = 0; n < 500; n++) {
        RigPose pose = model.spawnPose();
        IKGoal goal;
        goal.position = ankle + glm::vec3(0.08f * u(gen), 0.02f + 0.04f * (u(gen) + 1.f), -0.08f + 0.18f * u(gen));
        // The foot as at rest, turned a little about each axis
        glm::quat turn = glm::angleAxis(glm::radians(8.f) * u(gen), glm::vec3(1.f, 0.f, 0.f)) * glm::angleAxis(glm::radians(8.f) * u(gen), glm::vec3(0.f, 0.f, 1.f));
        goal.rotation = turn * rest.rotation[foot.segment];
        goal.pole = glm::vec3(0.f, 0.f, -1.f);
        IKSolve solve = solveChain(model, pose, { foot.hip, foot.knee, foot.ankle }, glm::vec3(0.f, 0.f, -1.f), goal);
        // A target that needs more than a joint allows is not reached: the limits win
        if(solve.limit_excess > 0.f) { clamped++; continue; }
        worst = std::max(worst, solve.position_error);
        worst_turn = std::max(worst_turn, solve.rotation_error);
        CHECK(swingTwist(pose.joint[foot.knee]).twist > 0.f);       // flexion
        SegmentPoses poses;
        model.forward(pose, poses);
        CHECK(model.anchor(poses, foot.knee).z < 0.5f * (model.anchor(poses, foot.hip).z + goal.position.z) + 1e-4f);    // the knee is in front
    }
    MESSAGE("worst ankle error ", worst * 1e3f, " mm, foot rotation error ", worst_turn * deg, " deg; ", clamped, " of 500 targets met a limit");
    CHECK(clamped < 50);
    CHECK(worst < 1e-4f);
    CHECK(worst_turn * deg < 0.1f);       // 0.056 degrees is one float step of the quaternion dot product
}

TEST_CASE("IK.7 a sole fitted on a tilted plane lies in it, and the rockers keep their edge on it") {
    if(!haveBody()) return;
    RigModel model(loadBody(body_file));
    for(bool right : { false, true }) {
        FootGeometry foot = footGeometry(model, right);
        PlaneField plane(18.f, -12.f);
        Footprint print = fitFootprint(plane, foot, glm::vec2(3.f, -7.f), 0.6f, 10.f);
        CHECK(print.gap < 1e-4f);
        CHECK(std::abs(plane.density(print.centre)) < 1e-4f);
        CHECK(glm::length(print.rotation * glm::vec3(0.f, 1.f, 0.f) - plane.normal()) < 1e-4f);

        glm::vec3 position;
        glm::quat rotation;
        footPose(foot, print, 0.f, 0, position, rotation);
        for(int c = 0; c < 4; c++) CHECK(std::abs(plane.density(foot.corner(c, position, rotation))) < 1e-4f);
        footPose(foot, print, 0.3f, +1, position, rotation);    // on the heel edge, toes up
        for(int c = 0; c < 2; c++) CHECK(std::abs(plane.density(foot.corner(c, position, rotation))) < 1e-4f);
        for(int c = 2; c < 4; c++) CHECK(plane.density(foot.corner(c, position, rotation)) < -0.05f);
        footPose(foot, print, -0.5f, -1, position, rotation);   // on the toe edge, heel up
        for(int c = 2; c < 4; c++) CHECK(std::abs(plane.density(foot.corner(c, position, rotation))) < 1e-4f);
        for(int c = 0; c < 2; c++) CHECK(plane.density(foot.corner(c, position, rotation)) < -0.1f);
    }
}

TEST_CASE("IK.8 the terrain field's surface is the colliders' mesh") {
    if(!std::filesystem::exists("resources/mclookup.bin")) return;
    ScenarioRunner runner("sphere_roll", { { "num_worlds", "1" }, { "spawn_radius", "0" }, { "stagger", "false" } }, SceneLoadOptions {});
    auto& world = runner.world(0);
    for(int t = 0; t < 30; t++) world.tick();
    auto view = world.reg.view<SphereAgent, Transform>();
    REQUIRE(view.begin() != view.end());
    glm::vec3 at = world.reg.get<Transform>(*view.begin()).position;
    auto& physics = world.reg.ctx().get<PhysicsWorld>();

    TerrainField field(std::make_shared<TerrainGenerator>(1337), std::make_shared<MarchingCubes>());
    std::mt19937 gen(3);
    std::uniform_real_distribution<float> u(-1.5f, 1.5f);
    int compared = 0;
    float worst = 0.f, worst_normal = 0.f;
    for(int n = 0; n < 200; n++) {
        glm::vec3 from = at + glm::vec3(u(gen), 3.f, u(gen));
        auto collider = physics.groundBelow(from, 8.f);
        auto surface = field.below(from, 8.f);
        if(!collider) continue;
        REQUIRE(surface.has_value());
        compared++;
        worst = std::max(worst, std::abs(collider->point.y - surface->point.y));
        worst_normal = std::max(worst_normal, glm::length(collider->normal - surface->normal));
    }
    MESSAGE(compared, " points, worst height difference ", worst * 1e3f, " mm, ", field.blocks(), " blocks");
    CHECK(compared > 100);
    CHECK(worst < 2e-3f);
    // Trilinear densities and the mesh agree at the grid's scale
    CHECK(std::abs(field.density(field.below(at + glm::vec3(0.f, 3.f, 0.f), 8.f)->point)) < 1.f);
}

}

TEST_SUITE("scenario") {

TEST_CASE("GAIT.1 the kinematic walk on level ground") {
    if(!haveBody()) return;
    Walk w = walk("ik_check", {}, 20.);
    MESSAGE(describe(w.report));
    checkWalk(w.report, 1.36f);
    CHECK(w.report.steps > 25);
    CHECK(w.report.max_capture_error < 0.03f);
    CHECK(w.report.max_body_limit_excess * deg < 0.1f);
    CHECK(w.state.step_length == doctest::Approx(0.76f).epsilon(0.08));
}

TEST_CASE("GAIT.2 slopes up, down and across, at several headings") {
    if(!haveBody()) return;
    // The plane's tilt and the heading; what the walker meets stays within 20 degrees along and 15 across its path
    struct Case { float pitch, roll, heading; };
    const Case cases[] = {
        { 10.f, 0.f, 0.f }, { -10.f, 0.f, 0.f }, { 20.f, 0.f, 0.f }, { -20.f, 0.f, 0.f },
        { 0.f, 15.f, 0.f }, { 0.f, -15.f, 0.f }, { 20.f, 15.f, 0.f }, { -20.f, -15.f, 0.f }, { 20.f, -15.f, 0.f }, { -20.f, 15.f, 0.f },
        { 20.f, 15.f, 180.f }, { 15.f, -19.f, 90.f }, { -15.f, 19.f, 90.f }, { 12.f, 12.f, 45.f }, { 14.f, -10.f, 300.f },
    };
    std::filesystem::create_directories("build/artifacts/ik");
    std::ofstream csv("build/artifacts/ik/slopes.csv");
    csv << "pitch,roll,heading,steps,speed,contact_mm,penetration_mm,slip_mm,sole_deg,ik_mm,ik_deg,ik_limit_deg,over_body_limits_deg,worst_joint,capture_mm,lateral_margin_mm\n";
    for(const Case& c : cases) {
        CAPTURE(c.pitch);
        CAPTURE(c.roll);
        CAPTURE(c.heading);
        Walk w = walk("ik_check", { { "pitch", std::to_string(c.pitch) }, { "roll", std::to_string(c.roll) }, { "heading", std::to_string(c.heading) } }, 15.);
        const GaitReport& r = w.report;
        CHECK(std::abs(w.state.slope_pitch) * deg < 21.f);
        CHECK(std::abs(w.state.slope_roll) * deg < 17.5f);
        MESSAGE("pitch ", c.pitch, " roll ", c.roll, " heading ", c.heading, " (the walker's ", w.state.slope_pitch * deg, " along, ", w.state.slope_roll * deg, " across): ", describe(r));
        csv << c.pitch << "," << c.roll << "," << c.heading << "," << r.steps << "," << r.mean_speed << "," << r.max_contact_error * 1e3f << "," << r.max_penetration * 1e3f << ","
            << r.max_slip * 1e3f << "," << r.max_sole_angle * deg << "," << r.max_ik_position * 1e3f << "," << r.max_ik_rotation * deg << "," << r.max_ik_limit * deg << ","
            << r.max_body_limit_excess * deg << "," << r.worst_joint << "," << r.max_capture_error * 1e3f << "," << r.min_lateral_margin * 1e3f << "\n";
        // At the edge of the range (20 degrees up and 15 across) a landing foot takes its ankle a degree past its cone
        checkWalk(r, slopeSpeed(1.36f, c.pitch, c.roll), 1.5f);
        CHECK(r.max_ik_rotation * deg < 1.f);
        CHECK(r.max_body_limit_excess * deg < 1.5f);
    }
}

TEST_CASE("GAIT.3 speeds from slow to fast") {
    if(!haveBody()) return;
    for(float speed : { 0.5f, 0.9f, 1.36f, 1.8f, 2.f }) {
        CAPTURE(speed);
        Walk w = walk("ik_check", { { "speed", std::to_string(speed) } }, 15.);
        MESSAGE("speed ", speed, ": step ", w.state.step_length, " m, stride ", w.state.stride_time, " s, stance ", w.state.stance_share, " | ", describe(w.report));
        checkWalk(w.report, speed);
    }
}

TEST_CASE("GAIT.4 starting, turning and stopping") {
    if(!haveBody()) return;
    Walk w = walk("ik_check", { { "pitch", "8" } }, 24., [](World& world, int tick) {
        auto view = world.reg.view<GaitCommand>();
        if(view.begin() == view.end()) return;
        auto& command = world.reg.get<GaitCommand>(*view.begin());
        if(tick == 6 * 60) command.heading = glm::radians(90.f);
        if(tick == 12 * 60) command.speed = 0.6f;
        if(tick == 16 * 60) command.speed = 0.f;
    });
    MESSAGE(describe(w.report));
    CHECK(w.report.max_contact_error < 5e-3f);
    CHECK(w.report.max_penetration < 5e-3f);
    CHECK(w.report.max_slip < 2e-3f);
    CHECK(w.report.max_limit_excess * deg < 0.1f);
    // Standing at the end: both feet flat, the clock stopped
    CHECK(w.state.foot[0].stance);
    CHECK(w.state.foot[1].stance);
    CHECK(w.state.speed == 0.f);
    CHECK(w.state.foot[0].contact == FootContact::Flat);
    CHECK(w.state.foot[1].contact == FootContact::Flat);
    CHECK(std::abs(w.state.heading - glm::radians(90.f)) < 1e-3f);
}

TEST_CASE("GAIT.8 joint speeds stay those of a walk: no jump between ticks, at strikes, toe-offs or on slopes") {
    if(!haveBody()) return;
    struct Case { const char* pitch; const char* roll; float most; };
    for(Case c : { Case { "0", "0", 700.f }, Case { "20", "0", 700.f }, Case { "-20", "0", 700.f }, Case { "12", "-12", 700.f } }) {
        ScenarioRunner runner("ik_check", { { "pitch", c.pitch }, { "roll", c.roll } }, SceneLoadOptions {});
        auto& world = runner.world(0);
        world.tick();
        entt::entity e = *world.reg.view<RigPose>().begin();
        const auto& model = *world.reg.get<Rig>(e).model;
        std::vector<glm::quat> before = world.reg.get<RigPose>(e).joint;
        glm::vec3 root = world.reg.get<RigPose>(e).root_position, root_velocity(0.f);
        std::vector<float> fastest(model.joints, 0.f);
        float root_jerk = 0.f;
        int at = 0, joint = 0;
        float worst = 0.f;
        for(int t = 1; t < 900; t++) {
            world.tick();
            const auto& pose = world.reg.get<RigPose>(e);
            glm::vec3 velocity = (pose.root_position - root) * 60.f;
            if(t > 180) root_jerk = std::max(root_jerk, glm::length(velocity - root_velocity) * 60.f);
            root_velocity = velocity;
            root = pose.root_position;
            for(int j = 0; j < model.joints; j++) {
                float rate = 2.f * std::acos(std::min(1.f, std::abs(glm::dot(pose.joint[j], before[j])))) * 60.f * deg;
                if(t > 180) {
                    fastest[j] = std::max(fastest[j], rate);
                    if(rate > worst) { worst = rate; at = t; joint = j; }
                }
            }
            before = pose.joint;
        }
        const auto& state = world.reg.get<GaitState>(e);
        const auto& g = state.foot[0].geometry;
        const std::string where = std::string("pitch ") + c.pitch + " roll " + c.roll;
        MESSAGE(where, ": hip ", fastest[g.hip], " knee ", fastest[g.knee], " ankle ", fastest[g.ankle], " deg/s; fastest joint ", model.body->joints[joint].name, " at tick ", at,
            "; pelvis acceleration up to ", root_jerk, " m/s2");
        CHECK(worst < c.most);
        CHECK(root_jerk < 25.f);
    }
}

TEST_CASE("GAIT.9 the observation lane runs on the kinematic walk") {
    if(!haveBody()) return;
    ScenarioRunner runner("ik_check", {}, SceneLoadOptions {});
    auto& world = runner.world(0);
    for(int t = 0; t < 300; t++) world.tick();
    const auto& io = world.reg.ctx().get<AgentIO>();
    REQUIRE(io.observation.size() == (size_t) humanbot_observations);
    float sum = 0.f, speed = 0.f;
    for(float v : io.observation) {
        CHECK(std::isfinite(v));
        sum += std::abs(v);
    }
    // The anchor velocities of the newer frame: the walk moves
    for(int j = 0; j < humanbot_joints; j++) for(int k = 6; k < 9; k++) speed += std::abs(io.observation[humanbot_frame + 3 + 9 * j + k]);
    CHECK(sum > 10.f);
    CHECK(speed > 5.f);
}

TEST_CASE("GAIT.5 the walk is deterministic") {
    if(!haveBody()) return;
    Walk a = walk("ik_check", { { "pitch", "12" }, { "roll", "7" } }, 5.);
    Walk b = walk("ik_check", { { "pitch", "12" }, { "roll", "7" } }, 5.);
    CHECK(a.hash == b.hash);
    CHECK(a.hash != 0);
}

TEST_CASE("GAIT.6 the marching cubes terrain, on routes whose slopes stay in range") {
    if(!haveBody() || !std::filesystem::exists("resources/mclookup.bin")) return;
    // Seed 1337 is steep almost everywhere (the worst slope of a 32 m line has a median of 44 degrees): these lines
    // stay under 18 degrees
    struct Route { float x, z, heading; };
    for(Route route : { Route { 0.f, 120.f, 90.f }, Route { 560.f, 480.f, 90.f }, Route { 160.f, -440.f, 270.f } }) {
        CAPTURE(route.x);
        CAPTURE(route.z);
        float steepest = 0.f, gap = 0.f, over = 0.f;
        Walk w = walk("ik_check_terrain", { { "start_x", std::to_string(route.x) }, { "start_z", std::to_string(route.z) }, { "heading", std::to_string(route.heading) } }, 24.,
            [&](World& world, int) {
                auto view = world.reg.view<GaitState, GaitReport>();
                if(view.begin() == view.end()) return;
                const auto& state = world.reg.get<GaitState>(*view.begin());
                const auto& report = world.reg.get<GaitReport>(*view.begin());
                steepest = std::max(steepest, std::hypot(state.slope_pitch, state.slope_roll));
                // The cubes are 4 m wide: a sole across an edge between two triangles rests on the edge
                for(const FootState& f : state.foot) if(f.stance) gap = std::max(gap, f.current.gap);
                over = std::max(over, report.contact_error - gap);
            });
        const GaitReport& r = w.report;
        MESSAGE("from ", route.x, " ", route.z, " heading ", route.heading, ": steepest ", steepest * deg, " deg, soles across edges up to ", gap * 1e3f, " mm | ", describe(r));
        CHECK(steepest * deg < 20.f);
        CHECK(r.max_penetration < 5e-3f);
        CHECK(r.max_slip < 2e-3f);
        CHECK(over < 5e-3f);
        CHECK(r.max_limit_excess * deg < 1.5f);
        CHECK(r.max_ik_position < 2e-3f);
        CHECK(r.max_ik_rotation * deg < 1.f);
        CHECK(r.max_outside < 1e-3f);
        CHECK(r.steps > 20);
    }
}

TEST_CASE("GAIT.7 any slope: on steep ground the feet stay planted and the joint limits give way") {
    if(!haveBody() || !std::filesystem::exists("resources/mclookup.bin")) return;
    struct Route { const char* scene; float pitch, x, z, heading; double seconds; };
    const Route routes[] = {
        { "ik_check", 35.f, 0.f, 0.f, 0.f, 15. }, { "ik_check", -35.f, 0.f, 0.f, 0.f, 15. }, { "ik_check", 50.f, 0.f, 0.f, 60.f, 15. },
        // The terrain's origin, where slopes pass 45 degrees
        { "ik_check_terrain", 0.f, 0.f, 0.f, 0.f, 40. }, { "ik_check_terrain", 0.f, 0.f, 0.f, 120.f, 40. }, { "ik_check_terrain", 0.f, 0.f, 0.f, 250.f, 40. },
    };
    for(const Route& route : routes) {
        float steepest = 0.f, over = 0.f;
        Walk w = walk(route.scene, { { "pitch", std::to_string(route.pitch) }, { "start_x", std::to_string(route.x) }, { "start_z", std::to_string(route.z) }, { "heading", std::to_string(route.heading) } },
            route.seconds, [&](World& world, int) {
                auto view = world.reg.view<GaitState, GaitReport>();
                if(view.begin() == view.end()) return;
                const auto& state = world.reg.get<GaitState>(*view.begin());
                const auto& report = world.reg.get<GaitReport>(*view.begin());
                steepest = std::max(steepest, std::hypot(state.slope_pitch, state.slope_roll));
                float gap = 0.f;
                for(const FootState& f : state.foot) if(f.stance) gap = std::max(gap, f.current.gap);
                if(state.time > report.settle) over = std::max(over, report.contact_error - gap);
            });
        const GaitReport& r = w.report;
        const std::string where = std::string(route.scene) + " pitch " + std::to_string((int) route.pitch) + " from " + std::to_string((int) route.x) + " " + std::to_string((int) route.z) + " heading " + std::to_string((int) route.heading);
        MESSAGE(where, ": steepest ", steepest * deg, " deg, limits gave way on ", r.yielded, " of ", r.ticks, " ticks | ", describe(r));
        CHECK(std::isfinite(w.balance.com.x + w.balance.com.y + w.balance.com.z));
        CHECK(r.steps > 8);
        CHECK(steepest * deg > 30.f);
        CHECK(over < 5e-3f);
        CHECK(r.max_penetration < 5e-3f);
        CHECK(r.max_slip < 2e-3f);
        CHECK(r.max_ik_position < 2e-3f);
        CHECK(r.max_ik_rotation * deg < 1.f);
    }
}

TEST_CASE("GAIT.11 a wall: the walker slows to short steps and stays whole") {
    if(!haveBody() || !std::filesystem::exists("resources/mclookup.bin")) return;
    // This line crosses an 80 degree face
    float steepest = 0.f, slowest = 10.f;
    Walk w = walk("ik_check_terrain", { { "start_x", "40" }, { "start_z", "40" }, { "heading", "180" } }, 90., [&](World& world, int tick) {
        auto view = world.reg.view<GaitState>();
        if(view.begin() == view.end() || tick < 300) return;
        const auto& state = world.reg.get<GaitState>(*view.begin());
        steepest = std::max(steepest, std::atan(state.steepness));
        slowest = std::min(slowest, state.speed);
    });
    const GaitReport& r = w.report;
    MESSAGE("steepest ", steepest * deg, " deg, slowest ", slowest, " m/s, limits gave way on ", r.yielded, " of ", r.ticks, " ticks | ", describe(r));
    CHECK(std::isfinite(w.balance.com.x + w.balance.com.y + w.balance.com.z));
    CHECK(steepest * deg > 70.f);
    CHECK(slowest < 0.3f);
    CHECK(slowest > 0.05f);
    CHECK(r.steps > 60);
    CHECK(r.max_ik_position < 0.05f);
    CHECK(r.max_penetration < 0.05f);
}

TEST_CASE("GAIT.10 with limits that hold, steep ground costs the foot its print, never the joints their limits") {
    if(!haveBody()) return;
    ScenarioRunner runner("ik_check", { { "pitch", "35" } }, SceneLoadOptions {});
    auto& world = runner.world(0);
    world.tick();
    entt::entity e = *world.reg.view<GaitParams>().begin();
    world.reg.get<GaitParams>(e).yield_limits = false;
    for(int t = 0; t < 900; t++) world.tick();
    const GaitReport& r = world.reg.get<GaitReport>(e);
    MESSAGE(describe(r));
    CHECK(r.yielded == 0);
    CHECK(r.max_limit_excess * deg < 0.1f);
    CHECK(r.max_ik_rotation * deg > 1.f);
}

}
