#include <doctest/doctest.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <set>

#include "logic/agents/agent_io.h"
#include "logic/agents/sphere_roller.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "physics/physics_world.h"
#include "physics/terrain_colliders.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

namespace {

// sphere_roll: marching cubes ground; sphere_roll_hills: a height field. Episodes not staggered unless asked
ScenarioRunner sphereRunner(const char* scene, int worlds, std::vector<std::pair<std::string, std::string>> vars = {}) {
    vars.emplace_back("num_worlds", std::to_string(worlds));
    bool stagger_given = false;
    for(auto& [k, v] : vars) stagger_given |= k == "stagger";
    if(!stagger_given) vars.emplace_back("stagger", "false");
    return ScenarioRunner(scene, vars, SceneLoadOptions {});
}

entt::entity agentEntity(World& w) {
    return *w.reg.view<SphereAgent>().begin();
}

glm::vec3 velocity(World& w) {
    auto& body = w.reg.get<RigidBody>(agentEntity(w));
    return toGlm(w.reg.ctx().get<PhysicsWorld>().bodies().GetLinearVelocity(JPH::BodyID(body.body)));
}

// What WorldsEnvironment::reset does: a fresh observation of every agent without stepping
void resetAll(ScenarioRunner& runner) {
    for(size_t i = 0; i < runner.worldCount(); i++) {
        runner.world(i).reg.ctx().get<AgentIO>().reset_requested = true;
        runner.world(i).systems.fixed_observe_only();
    }
}

uint32_t period(ScenarioRunner& runner) {
    return runner.world(0).reg.ctx().get<WorldClock>().decision_period;
}

// Mean window reward over `windows` decisions, every world driven by the heuristic or by zero actions; the first
// window of each episode (no decision yet, reward 0) is left out
double meanReward(ScenarioRunner& runner, int windows, bool heuristic) {
    double sum = 0.;
    int count = 0;
    std::vector<bool> fresh(runner.worldCount(), true);
    for(int k = 0; k < windows; k++) {
        for(size_t i = 0; i < runner.worldCount(); i++) {
            auto& io = runner.world(i).reg.ctx().get<AgentIO>();
            io.action = heuristic ? sphereHeuristic(io.observation) : std::vector<float>(3, 0.f);
        }
        runner.step(period(runner));
        for(size_t i = 0; i < runner.worldCount(); i++) {
            auto& io = runner.world(i).reg.ctx().get<AgentIO>();
            if(!fresh[i]) {
                sum += io.reward;
                count++;
            }
            fresh[i] = io.done;
        }
    }
    return sum / std::max(count, 1);
}

struct Slip {
    double static_share = 0.;   // of the ticks in contact on this tick and the last, those where the contact point slides < 5 cm/s
    double contact_share = 0.;  // of all ticks, those in contact
    double p99 = 0.;            // m/s, the 99th percentile of the sliding speed over the same ticks
};

// Every world driven by the proportional controller for `windows` decisions, sampled at every tick. In contact: Jolt
// finds the sphere penetrating static geometry (on a face, an edge or a vertex), so the contact carries load; the
// sliding speed is the tangential velocity of the sphere's material point there
Slip slip(ScenarioRunner& runner, int windows) {
    int ticks = 0, contact = 0;
    std::vector<float> sliding;
    std::vector<bool> before(runner.worldCount(), false);
    for(int k = 0; k < windows; k++) {
        for(size_t i = 0; i < runner.worldCount(); i++) {
            auto& io = runner.world(i).reg.ctx().get<AgentIO>();
            io.action = sphereHeuristic(io.observation);
        }
        for(uint32_t t = 0; t < period(runner); t++) {
            runner.step(1);
            for(size_t i = 0; i < runner.worldCount(); i++) {
                World& w = runner.world(i);
                auto& body = w.reg.get<RigidBody>(agentEntity(w));
                auto& physics = w.reg.ctx().get<PhysicsWorld>();
                JPH::BodyID id(body.body);
                glm::vec3 centre = toGlm(physics.bodies().GetCenterOfMassPosition(id));
                ticks++;
                auto hit = physics.sphereContact(centre, body.half_extent.x);
                if(hit) {
                    contact++;
                    glm::vec3 v = toGlm(physics.bodies().GetLinearVelocity(id)) + glm::cross(toGlm(physics.bodies().GetAngularVelocity(id)), hit->point - centre);
                    v -= glm::dot(v, hit->normal) * hit->normal;
                    if(before[i]) sliding.push_back(glm::length(v));
                }
                before[i] = (bool) hit;
            }
        }
    }
    std::sort(sliding.begin(), sliding.end());
    Slip out;
    if(!sliding.empty()) {
        out.static_share = (double) (std::lower_bound(sliding.begin(), sliding.end(), 0.05f) - sliding.begin()) / sliding.size();
        out.p99 = sliding[(size_t) (0.99 * (sliding.size() - 1))];
    }
    out.contact_share = ticks > 0 ? (double) contact / ticks : 0.;
    return out;
}

}

TEST_SUITE("unit") {

TEST_CASE("SR.1 the goal frame is a yaw rotation with y up and z along the target and x = up cross z") {
    for(float heading : { 0.f, 0.7f, 1.5707963f, 2.5f, -2.f, 3.1f }) {
        glm::vec3 forward = headingDirection(heading);
        GoalFrame f = GoalFrame::fromForward(forward);
        CHECK(glm::length(f.z - forward) < 1e-6f);
        CHECK(f.y == glm::vec3(0.f, 1.f, 0.f));
        CHECK(glm::length(f.x - glm::cross(glm::vec3(0.f, 1.f, 0.f), forward)) < 1e-6f);
        CHECK(std::abs(glm::dot(f.x, f.z)) < 1e-6f);
        // Proper rotation: det [x y z] = +1, so cross products (torque, angular velocity) carry over
        CHECK(glm::dot(f.x, glm::cross(f.y, f.z)) == doctest::Approx(1.f).epsilon(1e-6));
        glm::vec3 v(0.3f, -1.2f, 2.f);
        CHECK(glm::length(f.toWorld(f.toLocal(v)) - v) < 1e-5f);
        CHECK(glm::length(f.toLocal(glm::cross(v, forward)) - glm::cross(f.toLocal(v), f.toLocal(forward))) < 1e-5f);
    }
    // A sloped forward is flattened
    GoalFrame f = GoalFrame::fromForward(glm::vec3(0.f, 5.f, -1.f));
    CHECK(glm::length(f.z - glm::vec3(0.f, 0.f, -1.f)) < 1e-6f);
    // Engine axes (right-handed, Y up): for a target along -Z, x points to -X, the agent's left
    CHECK(glm::length(f.x - glm::vec3(-1.f, 0.f, 0.f)) < 1e-6f);
}

TEST_CASE("SR.2 reward is exp of minus the horizontal velocity error over the scale") {
    CHECK(sphereReward(glm::vec3(1.f, 0.f, 0.f), glm::vec3(1.f, 0.f, 0.f), 1.f) == doctest::Approx(1.f));
    CHECK(sphereReward(glm::vec3(1.f, 7.f, 0.f), glm::vec3(1.f, 0.f, 0.f), 1.f) == doctest::Approx(1.f));
    CHECK(sphereReward(glm::vec3(0.f, 0.f, 0.f), glm::vec3(3.f, 0.f, 4.f), 1.f) == doctest::Approx(std::exp(-5.f)));
    CHECK(sphereReward(glm::vec3(0.f, 0.f, 0.f), glm::vec3(3.f, 0.f, 4.f), 2.f) == doctest::Approx(std::exp(-2.5f)));
}

TEST_CASE("SR.3 heuristic torque is up cross the velocity error in the goal frame") {
    std::vector<float> obs(SphereObs::size, 0.f);
    obs[SphereObs::target_speed] = 0.5f;
    auto a = sphereHeuristic(obs, 1.f);
    // Forward (+z) needs a torque about +x: w x up = x x y = z
    CHECK(a[0] == doctest::Approx(0.5f));
    CHECK(a[1] == doctest::Approx(0.f));
    CHECK(a[2] == doctest::Approx(0.f));
    obs[SphereObs::velocity] = 0.25f;    // drifting along +x: the error has -x, up x -x = +z
    a = sphereHeuristic(obs, 1.f);
    CHECK(a[2] == doctest::Approx(0.25f));
    obs[SphereObs::target_speed] = 9.f;
    CHECK(sphereHeuristic(obs, 1.f)[0] == doctest::Approx(1.f));
}

TEST_CASE("SR.21 lean speed is 0 in the dead zone then logarithmic up to the top speed at the range") {
    // vr_roller's defaults: 1 cm dead zone, 4 m/s at 8 cm (3 doublings)
    CHECK(leanSpeed(0.f, 0.01f, 0.08f, 4.f) == 0.f);
    CHECK(leanSpeed(0.01f, 0.01f, 0.08f, 4.f) == 0.f);
    CHECK(leanSpeed(0.02f, 0.01f, 0.08f, 4.f) == doctest::Approx(4.f / 3.f));
    CHECK(leanSpeed(0.04f, 0.01f, 0.08f, 4.f) == doctest::Approx(8.f / 3.f));
    CHECK(leanSpeed(0.08f, 0.01f, 0.08f, 4.f) == 4.f);
    CHECK(leanSpeed(0.5f, 0.01f, 0.08f, 4.f) == 4.f);
    float last = 0.f;
    bool rising = true;
    for(float lean = 0.f; lean < 0.1f; lean += 0.001f) {
        float v = leanSpeed(lean, 0.01f, 0.08f, 4.f);
        rising = rising && v >= last && v <= 4.f;
        last = v;
    }
    CHECK(rising);
    CHECK(leanSpeed(0.0799f, 0.01f, 0.08f, 4.f) == doctest::Approx(4.f).epsilon(1e-3));
}

}

TEST_SUITE("scenario") {

TEST_CASE("SR.4 a window reward uses the target of the observation the action answered and not the live target") {

    auto runner = sphereRunner("sphere_roll", 1);
    World& w = runner.world(0);
    auto& io = w.reg.ctx().get<AgentIO>();
    auto& clock = w.reg.ctx().get<WorldClock>();
    REQUIRE(io.behavior == "SphereRoller");
    REQUIRE(io.observation_size == 16);
    REQUIRE(io.action_size == 3);
    REQUIRE(clock.decision_period == 4);

    resetAll(runner);
    auto& agent = w.reg.get<SphereAgent>(agentEntity(w));
    const float scale = agent.reward_scale;

    for(int window = 0; window < 6; window++) {
        // The decision target is the target the last observation carried
        const glm::vec3 observed = agent.decision_target;
        CHECK(glm::length(observed - agent.targetVelocity()) < 1e-6f);
        CHECK(io.observation[SphereObs::target_speed] == doctest::Approx(agent.speed));

        // Make the live target swing hard during the window: a goal behind, a fast turn
        agent.goal_heading = agent.heading + 3.f;
        agent.goal_speed = 3.f;
        agent.goal_timer = 100.f;
        agent.turn_rate = 6.f;
        agent.acceleration = 20.f;
        io.action = { 1.f, 0.f, 0.3f };

        double with_observed = 0., with_live = 0.;
        for(uint32_t t = 0; t < clock.decision_period; t++) {
            runner.step(1);
            glm::vec3 v = velocity(w);
            with_observed += sphereReward(v, observed, scale);
            with_live += sphereReward(v, agent.targetVelocity(), scale);
        }
        with_observed /= clock.decision_period;
        with_live /= clock.decision_period;
        MESSAGE("window " << window << ": reward " << io.reward << ", observed target " << with_observed << ", live target " << with_live);
        CHECK(io.reward == doctest::Approx(with_observed).epsilon(1e-5));
        CHECK(std::abs(with_observed - with_live) > 1e-3);
    }

}

TEST_CASE("SR.5 observations are goal frame vectors and height field normals") {

    auto runner = sphereRunner("sphere_roll_hills", 1);
    World& w = runner.world(0);
    auto& io = w.reg.ctx().get<AgentIO>();
    resetAll(runner);
    auto e = agentEntity(w);
    auto& agent = w.reg.get<SphereAgent>(e);
    auto fields = w.reg.view<HeightField>();
    const auto& hf = fields.get<HeightField>(*fields.begin());
    REQUIRE(hf.created());

    for(int window = 0; window < 40; window++) {
        io.action = { 0.8f, 0.f, 0.2f };
        runner.step(period(runner));
        if(io.done) continue;

        GoalFrame f = GoalFrame::fromForward(headingDirection(agent.heading));
        auto& body = w.reg.get<RigidBody>(e);
        auto& jolt = w.reg.ctx().get<PhysicsWorld>().bodies();
        glm::vec3 v = toGlm(jolt.GetLinearVelocity(JPH::BodyID(body.body)));
        glm::vec3 av = toGlm(jolt.GetAngularVelocity(JPH::BodyID(body.body)));
        auto get = [&](int i) { return glm::vec3(io.observation[i], io.observation[i + 1], io.observation[i + 2]); };

        CHECK(glm::length(get(SphereObs::velocity) - f.toLocal(v)) < 1e-5f);
        CHECK(glm::length(get(SphereObs::angular_velocity) - f.toLocal(av)) < 1e-4f);
        CHECK(io.observation[SphereObs::target_speed] == doctest::Approx(agent.speed));

        // The normal below against the smooth surface's normal (the collider is its 0.5 m triangulation)
        glm::vec3 p = w.reg.get<Transform>(e).position;
        const float d = 0.01f;
        glm::vec3 smooth = glm::normalize(glm::vec3(-(hf.height(p.x + d, p.z) - hf.height(p.x - d, p.z)) / (2 * d), 1.f,
                                                    -(hf.height(p.x, p.z + d) - hf.height(p.x, p.z - d)) / (2 * d)));
        glm::vec3 below = f.toWorld(get(SphereObs::normal_below));
        CHECK(glm::length(below) == doctest::Approx(1.f).epsilon(1e-4));
        CHECK(glm::dot(below, smooth) > 0.995f);
        for(int i : { SphereObs::normal_velocity, SphereObs::normal_target }) {
            CHECK(glm::length(get(i)) == doctest::Approx(1.f).epsilon(1e-4));
            CHECK(get(i).y > 0.8f);
        }
        // Normal toward the target: sampled probe_distance ahead along z of the goal frame
        glm::vec3 ahead = p + f.z * agent.probe_distance;
        glm::vec3 smooth_ahead = glm::normalize(glm::vec3(-(hf.height(ahead.x + d, ahead.z) - hf.height(ahead.x - d, ahead.z)) / (2 * d), 1.f,
                                                          -(hf.height(ahead.x, ahead.z + d) - hf.height(ahead.x, ahead.z - d)) / (2 * d)));
        CHECK(glm::dot(f.toWorld(get(SphereObs::normal_target)), smooth_ahead) > 0.995f);
    }

    // Flat ground: every normal is the goal frame's up
    auto flat = sphereRunner("sphere_roll_hills", 1, { { "wave_slope", "0.0" } });
    resetAll(flat);
    auto& flat_io = flat.world(0).reg.ctx().get<AgentIO>();
    for(int i : { SphereObs::normal_below, SphereObs::normal_velocity, SphereObs::normal_target }) {
        CHECK(flat_io.observation[i] == doctest::Approx(0.f).epsilon(1e-5));
        CHECK(flat_io.observation[i + 1] == doctest::Approx(1.f).epsilon(1e-5));
        CHECK(flat_io.observation[i + 2] == doctest::Approx(0.f).epsilon(1e-5));
    }

}

TEST_CASE("SR.6 the proportional controller tracks the drifting target and zero torque does not") {

    auto heuristic = sphereRunner("sphere_roll_hills", 8);
    resetAll(heuristic);
    double tracked = meanReward(heuristic, 900, true);
    auto idle = sphereRunner("sphere_roll_hills", 8);
    resetAll(idle);
    double still = meanReward(idle, 900, false);
    MESSAGE("height field, mean window reward over 8 worlds x 900 decisions: heuristic " << tracked << ", zero torque " << still);
    CHECK(tracked > 0.6);
    CHECK(still < 0.4);

}

TEST_CASE("SR.7 episodes end truncated after max_decisions or off the field and restart at the next window") {

    auto runner = sphereRunner("sphere_roll_hills", 1, { { "max_decisions", "20" } });
    World& w = runner.world(0);
    auto& io = w.reg.ctx().get<AgentIO>();
    resetAll(runner);

    int ends = 0;
    for(int k = 1; k <= 45; k++) {
        io.action = io.done ? std::vector<float>(3, 0.f) : std::vector<float> { 0.5f, 0.f, 0.f };
        runner.step(period(runner));
        // Observations 1..20 of the first episode, the 21st is the first of the second one (decision 1)
        bool expected = k == 20 || k == 40;
        CHECK(io.done == expected);
        if(io.done) {
            ends++;
            CHECK(io.truncated);
            bool found = false;
            for(auto& entry : io.stats) if(entry.key == "Sphere/Out of bounds") { found = true; CHECK(entry.value == 0.f); }
            CHECK(found);
        }
        if(k == 21 || k == 41) CHECK(io.reward == 0.f);
    }
    CHECK(ends == 2);

    // Off the field: the window ends parked, the episode with it
    auto e = agentEntity(w);
    auto& agent = w.reg.get<SphereAgent>(e);
    auto& body = w.reg.get<RigidBody>(e);
    if(io.done) {
        runner.step(period(runner));
    }
    glm::vec3 far(agent.bounds + 1.f, 5.f, 0.f);
    w.reg.ctx().get<PhysicsWorld>().bodies().SetPosition(JPH::BodyID(body.body), JPH::RVec3(toJolt(far)), JPH::EActivation::Activate);
    io.action = { 0.f, 0.f, 0.f };
    runner.step(period(runner));
    CHECK(io.done);
    CHECK(io.truncated);
    float out = -1.f;
    for(auto& entry : io.stats) if(entry.key == "Sphere/Out of bounds") out = entry.value;
    CHECK(out == 1.f);
    CHECK(!w.reg.ctx().get<Parked>().value);
    runner.step(period(runner));
    CHECK(!io.done);
    CHECK(std::abs(w.reg.get<Transform>(e).position.x) <= agent.spawn_radius + 1e-3f);

}

TEST_CASE("SR.8 the same seed gives the same rollout and another seed another rollout or terrain") {

    auto run = [](const char* scene, uint64_t seed) {
        auto runner = sphereRunner(scene, 3);
        runner.restart(seed);
        resetAll(runner);
        meanReward(runner, 50, true);
        std::vector<uint64_t> h;
        for(size_t i = 0; i < runner.worldCount(); i++) h.push_back(runner.world(i).hash());
        float height = 0.f;
        auto fields = runner.world(0).reg.view<HeightField>();
        if(fields.begin() != fields.end()) height = fields.get<HeightField>(*fields.begin()).height(3.f, 4.f);
        return std::make_pair(h, height);
    };
    // Marching cubes: one terrain (the seed of base), the trainer seed moves the spawn points and the targets
    auto a = run("sphere_roll", 7), b = run("sphere_roll", 7), c = run("sphere_roll", 8);
    CHECK(a.first == b.first);
    CHECK(a.first != c.first);
    // Height field: the seed draws the terrain too
    auto d = run("sphere_roll_hills", 7), e = run("sphere_roll_hills", 7), f = run("sphere_roll_hills", 8);
    CHECK(d.first == e.first);
    CHECK(d.second == e.second);
    CHECK(d.first != f.first);
    CHECK(d.second != f.second);

}

TEST_CASE("SR.11 marching cubes ground: the sphere spawns on it and the proportional controller beats zero torque") {

    auto runner = sphereRunner("sphere_roll", 8);
    resetAll(runner);
    for(size_t i = 0; i < runner.worldCount(); i++) {
        World& w = runner.world(i);
        auto& io = w.reg.ctx().get<AgentIO>();
        glm::vec3 p = w.reg.get<Transform>(agentEntity(w)).position;
        auto& terrain = *w.reg.ctx().get<std::shared_ptr<TerrainColliders>>();
        CHECK(terrain.activeCells() > 0);
        // On the ground, the generator surface give or take its overhangs
        auto hit = w.reg.ctx().get<PhysicsWorld>().groundBelow(p, 1.f);
        REQUIRE(hit);
        CHECK(p.y - hit->point.y == doctest::Approx(0.252f).epsilon(1e-3));
        CHECK(std::abs(p.y - terrain.surfaceHeight(p.x, p.z)) < terrain.overhangAmplitude() + 8.f);
        CHECK(glm::length(glm::vec2(p.x, p.z)) <= 2000.f);
        glm::vec3 below(io.observation[SphereObs::normal_below], io.observation[SphereObs::normal_below + 1], io.observation[SphereObs::normal_below + 2]);
        CHECK(glm::length(below) == doctest::Approx(1.f).epsilon(1e-4));
    }

    double tracked = meanReward(runner, 900, true);
    bool finite = true;
    for(size_t i = 0; i < runner.worldCount(); i++) {
        for(float v : runner.world(i).reg.ctx().get<AgentIO>().observation) finite = finite && std::isfinite(v);
    }
    auto idle = sphereRunner("sphere_roll", 8);
    resetAll(idle);
    double still = meanReward(idle, 900, false);
    MESSAGE("marching cubes, mean window reward over 8 worlds x 900 decisions: heuristic " << tracked << ", zero torque " << still);
    CHECK(finite);
    CHECK(tracked > still + 0.3);

}

TEST_CASE("SR.12 the trainer RESET staggers episodes so worlds do not all end together") {

    auto runner = sphereRunner("sphere_roll", 8, { { "stagger", "true" }, { "max_decisions", "40" } });
    resetAll(runner);
    std::set<int> first_end;
    std::vector<bool> ended(runner.worldCount(), false);
    for(int k = 1; k <= 41; k++) {
        meanReward(runner, 1, false);
        for(size_t i = 0; i < runner.worldCount(); i++) {
            if(!ended[i] && runner.world(i).reg.ctx().get<AgentIO>().done) {
                ended[i] = true;
                first_end.insert(k);
            }
        }
    }
    CHECK(std::count(ended.begin(), ended.end(), true) == 8);
    CHECK(first_end.size() >= 4);

}

TEST_CASE("SR.17 a steered agent takes its goal from steerSphere only and restarts at rest without timing out") {

    auto runner = sphereRunner("sphere_roll", 1, { { "steered", "true" }, { "spawn_radius", "0" }, { "max_decisions", "5" } });
    World& w = runner.world(0);
    auto& io = w.reg.ctx().get<AgentIO>();
    auto e = agentEntity(w);
    auto& agent = w.reg.get<SphereAgent>(e);
    resetAll(runner);
    REQUIRE(agent.steered);
    CHECK(agent.speed == 0.f);
    CHECK(agent.goal_speed == 0.f);
    glm::vec3 spawn = w.reg.get<Transform>(e).position;
    CHECK(std::abs(spawn.x) < 1e-4f);
    CHECK(std::abs(spawn.z) < 1e-4f);

    // The heading and speed of the horizontal command, also beyond training's speed_range (the lean's 4 m/s); a null
    // command keeps the heading
    steerSphere(agent, glm::vec3(0.f, 5.f, -2.f));
    CHECK(agent.goal_heading == doctest::Approx(std::atan2(-2.f, 0.f)));
    CHECK(agent.goal_speed == doctest::Approx(2.f));
    steerSphere(agent, glm::vec3(4.f, 0.f, 0.f));
    CHECK(agent.goal_heading == doctest::Approx(0.f));
    CHECK(agent.goal_speed == doctest::Approx(4.f));
    steerSphere(agent, glm::vec3(0.f));
    CHECK(agent.goal_heading == doctest::Approx(0.f));
    CHECK(agent.goal_speed == 0.f);

    // 60 decisions under a held command (12 x max_decisions, several goal intervals): no goal drawn, no episode end,
    // the live target reaches the command at its rates and the controller rolls the sphere along it
    steerSphere(agent, 2.f * headingDirection(1.f));
    for(int k = 0; k < 60; k++) {
        io.action = sphereHeuristic(io.observation);
        runner.step(period(runner));
        CHECK(!io.done);
        CHECK(agent.goal_heading == doctest::Approx(1.f));
        CHECK(agent.goal_speed == doctest::Approx(2.f));
    }
    CHECK(agent.decisions == 60);
    CHECK(agent.heading == doctest::Approx(1.f));
    CHECK(agent.speed == doctest::Approx(2.f));
    glm::vec3 v = velocity(w);
    CHECK(glm::length(glm::vec3(v.x, 0.f, v.z) - agent.targetVelocity()) < 0.7f);

    // A fall ends the episode; the next starts at the spawn point, at rest, facing the player's heading
    auto& body = w.reg.get<RigidBody>(e);
    glm::vec3 deep(10.f, agent.home.y - agent.fall_depth - 20.f, 10.f);
    w.reg.ctx().get<PhysicsWorld>().bodies().SetPosition(JPH::BodyID(body.body), JPH::RVec3(toJolt(deep)), JPH::EActivation::Activate);
    io.action = { 0.f, 0.f, 0.f };
    runner.step(period(runner));
    CHECK(io.done);
    runner.step(period(runner));
    CHECK(!io.done);
    glm::vec3 p = w.reg.get<Transform>(e).position;
    CHECK(glm::length(glm::vec2(p.x - spawn.x, p.z - spawn.z)) < 0.05f);
    CHECK(agent.heading == doctest::Approx(1.f));
    CHECK(agent.speed == 0.f);
    CHECK(agent.goal_speed == 0.f);

}

TEST_CASE("SR.22 wheel_roll: a sphere of the walker's hip height as radius, a human's mass, mu 4 and the small sphere's torque ratio") {

    auto runner = sphereRunner("wheel_roll", 8);
    resetAll(runner);
    for(size_t i = 0; i < runner.worldCount(); i++) {
        World& w = runner.world(i);
        auto e = agentEntity(w);
        auto& agent = w.reg.get<SphereAgent>(e);
        auto& body = w.reg.get<RigidBody>(e);
        auto& physics = w.reg.ctx().get<PhysicsWorld>();
        CHECK(agent.behavior == "WheelRoller");
        CHECK(agent.max_torque == 520.f);
        CHECK(body.half_extent.x == 0.94f);
        CHECK(body.friction == 4.f);
        CHECK(physicsConfig(runner.scene().worlds[0]).friction == Combine::Max);

        // At rest on the ground: the centre of mass at hip height
        glm::vec3 p = w.reg.get<Transform>(e).position;
        auto hit = physics.groundBelow(p, 1.5f);
        REQUIRE(hit);
        CHECK(p.y - hit->point.y == doctest::Approx(0.942f).epsilon(1e-3));
    }

    // Jolt bodies exist from the first tick on
    runner.step(period(runner));
    for(size_t i = 0; i < runner.worldCount(); i++) {
        World& w = runner.world(i);
        auto& body = w.reg.get<RigidBody>(agentEntity(w));
        auto& physics = w.reg.ctx().get<PhysicsWorld>();
        REQUIRE(body.created());
        // Mass properties of a solid sphere, as Jolt applies them: an impulse of 69 N s changes the velocity by 1 m/s, an
        // angular impulse of I = 2/5 m r^2 N m s the angular velocity by 1 rad/s about any axis; the centre of mass at the centre
        JPH::BodyID id(body.body);
        glm::vec3 v0 = toGlm(physics.bodies().GetLinearVelocity(id)), w0 = toGlm(physics.bodies().GetAngularVelocity(id));
        physics.bodies().AddImpulse(id, JPH::Vec3(0.f, 69.f, 0.f));
        CHECK(glm::length(toGlm(physics.bodies().GetLinearVelocity(id)) - v0 - glm::vec3(0.f, 1.f, 0.f)) < 1e-4f);
        const float inertia = 0.4f * 69.f * 0.94f * 0.94f;
        for(glm::vec3 axis : { glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f), glm::normalize(glm::vec3(1.f, 1.f, -1.f)) }) {
            glm::vec3 before = toGlm(physics.bodies().GetAngularVelocity(id));
            physics.bodies().AddAngularImpulse(id, toJolt(axis * inertia));
            CHECK(glm::length(toGlm(physics.bodies().GetAngularVelocity(id)) - before - axis) < 1e-4f);
        }
        physics.bodies().SetLinearAndAngularVelocity(id, toJolt(v0), toJolt(w0));
        CHECK(glm::length(toGlm(physics.bodies().GetCenterOfMassPosition(id)) - toGlm(physics.bodies().GetPosition(id))) < 1e-6f);

    }

    double tracked = meanReward(runner, 900, true);
    auto idle = sphereRunner("wheel_roll", 8);
    resetAll(idle);
    double still = meanReward(idle, 900, false);
    MESSAGE("wheel, mean window reward over 8 worlds x 900 decisions: heuristic " << tracked << ", zero torque " << still);
    CHECK(tracked > still + 0.3);

}

TEST_CASE("SR.23 wheel_roll: under the proportional controller the loaded contact sticks on nearly every tick") {

    auto wheel = sphereRunner("wheel_roll", 8);
    resetAll(wheel);
    Slip w = slip(wheel, 450);
    auto sphere = sphereRunner("sphere_roll", 8);
    resetAll(sphere);
    Slip s = slip(sphere, 450);
    MESSAGE("8 worlds x 450 decisions: wheel (mu 4) static " << w.static_share << " (sliding p99 " << w.p99 << " m/s, loaded contact " << w.contact_share
        << " of the ticks); small sphere (mu 0.89) static " << s.static_share << " (p99 " << s.p99 << ", loaded contact " << s.contact_share << ")");
    CHECK(w.static_share > 0.97);
    CHECK(w.p99 < 0.2);

}

}
