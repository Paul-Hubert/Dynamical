#include <doctest/doctest.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>

#include "logic/agents/agent_io.h"
#include "logic/agents/sphere_roller.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/systems/system_set.h"
#include "onnx/policy_runner.h"
#include "onnx/policy_systems.h"
#include "physics/physics_world.h"
#include "scene/scenario_runner.h"
#include "scene/scene_def.h"
#include "scene/system_registry.h"
#include "scene/world.h"
#include "support/test_systems.h"

using namespace dy;

namespace {

const char* shipped = "resources/policies/sphere_roller.onnx";

struct Tracking {
    double target_error = 0.;   // m/s, against the live target
    double command_error = 0.;  // m/s, against the player's command
    double held_error = 0.;     // m/s, against the command, over the last second of each command held 3 s or more
    double held_speed = 0.;     // m/s, the horizontal speed over the same samples
};

// A player on vr_roller for `seconds`: a new command every 1.5 to 4 s (any heading change, 0 to top m/s, 15% stops;
// always top with flat_out), the sphere driven by the policy or by the proportional controller. Errors sampled at
// every decision
Tracking play(std::shared_ptr<PolicyRunner> policy, double seconds, uint32_t seed, float top = 3.f, bool flat_out = false,
    std::vector<std::pair<std::string, std::string>> vars = {}) {
    ScenarioRunner runner("vr_roller", vars, SceneLoadOptions {});
    World& w = runner.world(0);
    auto& io = w.reg.ctx().get<AgentIO>();
    entt::entity e = *w.reg.view<SphereAgent>().begin();
    auto& agent = w.reg.get<SphereAgent>(e);
    auto& body = w.reg.get<RigidBody>(e);
    if(policy) w.reg.ctx().insert_or_assign(PolicyHandle { policy, true });
    io.reset_requested = true;
    w.systems.fixed_observe_only();

    const auto& clock = w.reg.ctx().get<WorldClock>();
    const double window = clock.decision_period * clock.dt;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uniform(0.f, 1.f);
    float heading = 0.f, speed = 0.f;
    double left = 0., held = 0.;
    double target_sum = 0., command_sum = 0., held_sum = 0., speed_sum = 0.;
    int samples = 0, held_samples = 0;
    for(double t = 0.; t < seconds; t += window) {
        if(left <= 0.) {
            heading += 6.2831853f * (uniform(rng) - 0.5f);
            speed = flat_out ? top : uniform(rng) < 0.15f ? 0.f : top * uniform(rng);
            left = 1.5 + 2.5 * uniform(rng);
            held = 0.;
        }
        glm::vec3 command = speed * headingDirection(heading);
        steerSphere(agent, command);
        if(!policy) io.action = sphereHeuristic(io.observation);
        runner.step(clock.decision_period);
        left -= window;
        held += window;

        glm::vec3 v = toGlm(w.reg.ctx().get<PhysicsWorld>().bodies().GetLinearVelocity(JPH::BodyID(body.body)));
        v.y = 0.f;
        target_sum += glm::length(v - agent.targetVelocity());
        command_sum += glm::length(v - command);
        samples++;
        if(held > 2. && held + left >= 3.) {
            held_sum += glm::length(v - command);
            speed_sum += glm::length(v);
            held_samples++;
        }
    }
    return { target_sum / samples, command_sum / samples, held_sum / std::max(held_samples, 1), speed_sum / std::max(held_samples, 1) };
}

}

TEST_SUITE("unit") {

TEST_CASE("SR.18 vr_roller: VRRoller between WorldStepper and VRControl owns the desktop camera and one steered sphere is driven by the shipped policy") {

    test::registerViewSystemStubs();
    SceneDef def = loadScene("vr_roller", {}, SceneLoadOptions {});
    REQUIRE(def.renderer);
    CHECK(def.renderer->vr);
    std::vector<std::string> pre;
    for(auto& entry : def.host_pre) pre.push_back(entry.name);
    auto at = [&](const char* name) { return std::find(pre.begin(), pre.end(), name) - pre.begin(); };
    CHECK(at("VRRoller") < (long long) pre.size());
    CHECK(at("WorldStepper") < at("VRRoller"));
    CHECK(at("VRRoller") < at("VRControl"));
    CHECK(at("ModelWatcher") < at("WorldStepper"));
    // The desktop player: VRRoller places the camera PlayerControl turned, SphereView leaves it alone
    CHECK(at("PlayerControl") < at("VRRoller"));
    auto view = std::find_if(def.host_pre.begin(), def.host_pre.end(), [](const SystemEntry& e) { return e.name == "SphereView"; });
    REQUIRE(view != def.host_pre.end());
    CHECK(view->params.value("camera", true) == false);

    REQUIRE(def.worlds.size() == 1);
    CHECK(def.worlds[0].count == 1);
    bool policy = false, trainer = false;
    for(auto& s : def.worlds[0].fixed) {
        policy |= s.name == "Policy" && s.phase == Phase::Act;
        trainer |= s.name == "TrainerActions";
    }
    CHECK(policy);
    CHECK(!trainer);

    ScenarioRunner runner("vr_roller", {}, SceneLoadOptions {});
    auto& reg = runner.world(0).reg;
    const auto& agent = reg.get<SphereAgent>(*reg.view<SphereAgent>().begin());
    CHECK(agent.steered);
    CHECK(agent.spawn_radius == 0.f);
    CHECK(agent.turn_rate == 1.5f);
    CHECK(agent.acceleration == 1.5f);
    PolicyRunner model(shipped, SphereObs::size, sphere_actions);
    CHECK(model.info().memory_size == 0);

    // The lean's scene variables reach VRRoller
    auto roller = std::find_if(def.host_pre.begin(), def.host_pre.end(), [](const SystemEntry& e) { return e.name == "VRRoller"; });
    REQUIRE(roller != def.host_pre.end());
    CHECK(roller->params.value("lean_speed", 0.) == 3.);
    CHECK(roller->params.value("lean_range", 0.) == 0.08);
    SceneDef faster = loadScene("vr_roller", { { "lean_speed", "5" }, { "lean_range", "0.12" } }, SceneLoadOptions {});
    roller = std::find_if(faster.host_pre.begin(), faster.host_pre.end(), [](const SystemEntry& e) { return e.name == "VRRoller"; });
    CHECK(roller->params.value("lean_speed", 0.) == 5.);
    CHECK(roller->params.value("lean_range", 0.) == 0.12);

}

}

TEST_SUITE("scenario") {

TEST_CASE("SR.19 on vr_roller the shipped policy follows a player's commands better than the proportional controller") {

    test::registerViewSystemStubs();
    auto policy = std::make_shared<PolicyRunner>(shipped, SphereObs::size, sphere_actions);
    Tracking trained, controller;
    const int players = 6;
    for(uint32_t seed = 1; seed <= players; seed++) {
        Tracking p = play(policy, 60., seed), c = play(nullptr, 60., seed);
        MESSAGE("player " << seed << ": policy target " << p.target_error << " command " << p.command_error << " held " << p.held_error
            << " | controller target " << c.target_error << " command " << c.command_error << " held " << c.held_error);
        trained.target_error += p.target_error / players;
        trained.command_error += p.command_error / players;
        trained.held_error += p.held_error / players;
        controller.target_error += c.target_error / players;
        controller.command_error += c.command_error / players;
        controller.held_error += c.held_error / players;
    }
    MESSAGE(players << " players x 60 s, mean speed error (m/s): policy target " << trained.target_error << " held " << trained.held_error
        << ", controller target " << controller.target_error << " held " << controller.held_error);
    CHECK(trained.target_error < controller.target_error);
    CHECK(trained.held_error < controller.held_error);
    // Within a quarter of a m/s of the stick once the target has caught up with it
    CHECK(trained.held_error < 0.25);

}

}

TEST_SUITE("scenario") {

TEST_CASE("SR.20 on vr_roller the shipped policy reaches 4 m/s when var.lean_speed is 4 and follows commands up to it better than the controller") {

    test::registerViewSystemStubs();
    auto policy = std::make_shared<PolicyRunner>(shipped, SphereObs::size, sphere_actions);
    Tracking trained, controller, flat_out;
    const int players = 6;
    for(uint32_t seed = 1; seed <= players; seed++) {
        Tracking p = play(policy, 60., seed, 4.f), c = play(nullptr, 60., seed, 4.f), f = play(policy, 60., seed, 4.f, true);
        trained.held_error += p.held_error / players;
        controller.held_error += c.held_error / players;
        flat_out.held_speed += f.held_speed / players;
    }
    // Training's targets stop at 3 m/s: above it the policy tracks less well (0.19 m/s up to 3 in SR.19)
    MESSAGE(players << " players x 60 s up to 4 m/s, held error (m/s): policy " << trained.held_error << ", controller " << controller.held_error
        << "; always 4 m/s: policy's mean held speed " << flat_out.held_speed);
    CHECK(trained.held_error < controller.held_error);
    CHECK(flat_out.held_speed == doctest::Approx(4.).epsilon(0.1));

}

}
