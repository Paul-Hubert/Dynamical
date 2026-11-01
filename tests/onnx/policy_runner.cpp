#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <memory>

#include "logic/agents/agent_io.h"
#include "logic/agents/sphere_roller.h"
#include "onnx/policy_runner.h"
#include "onnx/policy_systems.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

namespace {

const char* model = "tests/data/onnx/sphere_roller.onnx";

// Mean window reward of every world over `windows` decisions, actions from `act` (first windows of episodes left out)
template<typename Act>
double meanReward(ScenarioRunner& runner, int windows, Act act) {
    double sum = 0.;
    int count = 0;
    std::vector<bool> fresh(runner.worldCount(), true);
    const uint32_t period = runner.world(0).reg.ctx().get<WorldClock>().decision_period;
    for(int k = 0; k < windows; k++) {
        for(size_t i = 0; i < runner.worldCount(); i++) act(runner.world(i).reg.ctx().get<AgentIO>());
        runner.step(period);
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

void resetAll(ScenarioRunner& runner) {
    for(size_t i = 0; i < runner.worldCount(); i++) {
        runner.world(i).reg.ctx().get<AgentIO>().reset_requested = true;
        runner.world(i).systems.fixed_observe_only();
    }
}

}

TEST_SUITE("unit") {

TEST_CASE("SR.13 PolicyRunner matches Python onnxruntime on 100 observations") {

    std::ifstream in("tests/data/onnx/sphere_roller.reference.json");
    REQUIRE(in);
    auto ref = nlohmann::json::parse(in);
    PolicyRunner runner(model, SphereObs::size, sphere_actions);
    CHECK(runner.info().memory_size == 0);
    CHECK(runner.info().version == (int) ref["version_number"].get<float>());

    auto& observations = ref["observations"];
    auto& expected = ref["deterministic_actions"];
    REQUIRE(observations.size() == 100);
    std::vector<float> obs, actions(100 * 3);
    for(auto& row : observations) for(auto& v : row) obs.push_back(v.get<float>());

    // One batch of 100, then one at a time: the same numbers
    runner.run(obs.data(), 100, actions.data());
    double worst = 0.;
    for(int i = 0; i < 100; i++) {
        for(int a = 0; a < 3; a++) worst = std::max(worst, (double) std::abs(actions[i * 3 + a] - expected[i][a].get<float>()));
    }
    MESSAGE("worst difference against Python onnxruntime " << ref["onnxruntime"].get<std::string>() << ": " << worst);
    CHECK(worst <= 1e-5);
    for(int i = 0; i < 100; i += 17) {
        float single[3];
        runner.run(&obs[i * 16], 1, single);
        for(int a = 0; a < 3; a++) CHECK(single[a] == doctest::Approx(actions[i * 3 + a]).epsilon(1e-6));
    }

}

TEST_CASE("SR.14 PolicyRunner refuses a model of another layout or a missing file") {
    CHECK_THROWS_AS(PolicyRunner(model, 17, 3), PolicyError);
    CHECK_THROWS_AS(PolicyRunner(model, 16, 4), PolicyError);
    CHECK_THROWS_AS(PolicyRunner("tests/data/onnx/missing.onnx", 16, 3), PolicyError);
    try {
        PolicyRunner(model, 17, 3);
    } catch(const PolicyError& e) {
        CHECK(std::string(e.what()).find("16") != std::string::npos);
    }
}

TEST_CASE("SR.15 ModelWatcher latest picks the largest step and across runs the most recent run") {
    auto dir = std::filesystem::temp_directory_path() / "dynamical_latest";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    for(const char* name : { "SphereRoller-999.onnx", "SphereRoller-10000.onnx", "SphereRoller-2500.onnx", "SphereRoller.onnx", "notes.txt" }) {
        std::ofstream(dir / name) << "x";
    }
    int64_t step = 0;
    CHECK(ModelWatcherSys::latest(dir, &step).filename() == "SphereRoller-10000.onnx");
    CHECK(step == 10000);
    std::filesystem::remove_all(dir);
    CHECK(ModelWatcherSys::latest(dir, &step).empty());

    // A '*' component: the run whose newest checkpoint is the most recent, not the largest step
    auto runs = std::filesystem::temp_directory_path() / "dynamical_latest_runs";
    std::filesystem::remove_all(runs);
    for(const char* run : { "wheel_a", "wheel_b", "sphere_c" }) std::filesystem::create_directories(runs / run / "WheelRoller");
    std::ofstream(runs / "wheel_a" / "WheelRoller" / "WheelRoller-90000.onnx") << "x";
    std::ofstream(runs / "wheel_b" / "WheelRoller" / "WheelRoller-500.onnx") << "x";
    std::ofstream(runs / "sphere_c" / "WheelRoller" / "WheelRoller-700.onnx") << "x";
    auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(runs / "wheel_a" / "WheelRoller" / "WheelRoller-90000.onnx", now - std::chrono::hours(2));
    std::filesystem::last_write_time(runs / "wheel_b" / "WheelRoller" / "WheelRoller-500.onnx", now - std::chrono::hours(1));
    std::filesystem::last_write_time(runs / "sphere_c" / "WheelRoller" / "WheelRoller-700.onnx", now);
    auto found = ModelWatcherSys::latest(runs / "wheel_*" / "WheelRoller", &step);
    CHECK(found.parent_path().parent_path().filename() == "wheel_b");
    CHECK(step == 500);
    CHECK(ModelWatcherSys::latest(runs / "*" / "WheelRoller", &step).parent_path().parent_path().filename() == "sphere_c");
    CHECK(ModelWatcherSys::latest(runs / "none_*" / "WheelRoller", &step).empty());
    std::filesystem::remove_all(runs);
}

}

TEST_SUITE("scenario") {

TEST_CASE("SR.16 the Policy system writes the policy action and the trained policy beats the controller on marching cubes") {

    auto policy = std::make_shared<PolicyRunner>(model, SphereObs::size, sphere_actions);

    // Policy (world, Act phase) through a handle: the same action as a direct run on the observation
    {
        ScenarioRunner runner("sphere_roll", { { "num_worlds", "1" }, { "stagger", "false" } }, SceneLoadOptions {});
        resetAll(runner);
        auto& reg = runner.world(0).reg;
        reg.ctx().insert_or_assign(PolicyHandle { policy, true });
        auto& io = reg.ctx().get<AgentIO>();
        float expected[3];
        policy->run(io.observation.data(), 1, expected);
        PolicySys(reg).tick(1. / 60.);
        for(int a = 0; a < 3; a++) CHECK(io.action[a] == expected[a]);
    }

    // The policy against the proportional controller, same worlds and seeds
    auto run = [&](bool use_policy) {
        ScenarioRunner runner("sphere_roll", { { "num_worlds", "8" }, { "stagger", "false" } }, SceneLoadOptions {});
        resetAll(runner);
        return meanReward(runner, 900, [&](AgentIO& io) {
            if(use_policy) policy->run(io.observation.data(), 1, io.action.data());
            else io.action = sphereHeuristic(io.observation);
        });
    };
    double trained = run(true), controller = run(false);
    MESSAGE("marching cubes, 8 worlds x 900 decisions: policy " << trained << ", proportional controller " << controller);
    CHECK(trained > controller + 0.05);

}

}
