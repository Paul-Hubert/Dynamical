#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "logic/agents/agent_io.h"
#include "logic/agents/pursuit.h"
#include "onnx/agent_policy.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

// Policies of agents that are components (docs/action-agents.md, inference): a model with two observations (a vector
// and a buffer), continuous actions and a discrete branch, exported by the trainer from the pursuit fixture
// (chasers trained for 400k steps against runners that stand still, with the approach reward)

namespace {

const char* model = "tests/data/onnx/pursuit_chaser.onnx";

uint64_t catches(ScenarioRunner& runner, int windows) {
    for(size_t i = 0; i < runner.worldCount(); i++) {
        runner.world(i).reg.ctx().get<AgentsReset>().requested = true;
        runner.world(i).systems.fixed_observe_only();
    }
    runner.step((int64_t) windows * runner.world(0).reg.ctx().get<WorldClock>().decision_period);
    uint64_t total = 0;
    for(size_t i = 0; i < runner.worldCount(); i++) total += runner.world(i).reg.ctx().get<PursuitField>().catches;
    return total;
}

}

TEST_SUITE("onnx") {

TEST_CASE("AP.1 a model with a vector and a buffer observation, continuous actions and a discrete branch runs on its agent") {
    AgentPolicy policy(model);
    CHECK(policy.continuousActions() == 2);
    CHECK(policy.branches().size() == 1);
    CHECK(policy.describe() == "obs_0 [6], obs_1 [8 x 6] -> 2 continuous, 1 discrete");

    AgentIO io;
    io.configure("Chaser", PursuitObs::size, pursuit_actions);
    io.addBranch(2);
    io.addBuffer("Others", 8, PursuitObs::row);
    // A runner straight ahead, then one to the left: the chaser turns toward where it is
    io.observation = { 0.f, 0.f, 0.f, 1.f, 0.5f, 1.f };
    float* row = io.sensors[0].add();
    row[PursuitObs::other_position] = 0.2f;
    row[PursuitObs::other_runner] = 1.f;
    policy.run(io, true);
    const float ahead = io.action[1];
    CHECK(std::isfinite(io.action[0]));
    CHECK(io.action[0] >= -1.f);
    CHECK(io.action[0] <= 1.f);
    CHECK(io.discrete[0] >= 0);
    CHECK(io.discrete[0] <= 1);
    row[PursuitObs::other_position] = 0.f;
    row[PursuitObs::other_position + 1] = 0.2f;
    policy.run(io, true);
    CHECK(io.action[1] > ahead + 0.2f);
    row[PursuitObs::other_position + 1] = -0.2f;
    policy.run(io, true);
    CHECK(io.action[1] < ahead - 0.2f);
    // The same input gives the same deterministic output; a sampled one moves around it
    const float right = io.action[1];
    policy.run(io, true);
    CHECK(io.action[1] == right);
    bool varied = false;
    for(int i = 0; i < 8; i++) {
        policy.run(io, false);
        varied = varied || io.action[1] != right;
    }
    CHECK(varied);

    // Another layout is refused with the reason
    AgentIO other;
    other.configure("Chaser", 5, pursuit_actions);
    other.addBranch(2);
    other.addBuffer("Others", 8, PursuitObs::row);
    CHECK_THROWS_AS(policy.run(other, true), PolicyError);
    AgentIO branches = io;
    branches.branches = { 3 };
    CHECK_THROWS_AS(policy.run(branches, true), PolicyError);
    CHECK_THROWS_AS(AgentPolicy("tests/data/onnx/missing.onnx"), PolicyError);
}

TEST_CASE("AP.2 a behaviour's model: the run's export, else its newest checkpoint") {
    const auto dir = std::filesystem::temp_directory_path() / "dy_agent_policy_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "Chaser");
    CHECK(findBehaviorModel(dir, "Chaser").empty());
    for(const char* name : { "Chaser-999.onnx", "Chaser-10000.onnx", "Chaser-2500.onnx", "notes.txt" }) {
        std::filesystem::copy_file(model, dir / "Chaser" / name);
    }
    CHECK(findBehaviorModel(dir, "Chaser").filename() == "Chaser-10000.onnx");
    CHECK(findBehaviorModel(dir, "Runner").empty());
    std::filesystem::copy_file(model, dir / "Chaser.onnx");
    CHECK(findBehaviorModel(dir, "Chaser") == dir / "Chaser.onnx");
    std::filesystem::remove_all(dir);
}

TEST_CASE("AP.3 Policies in the worlds: trained chasers catch runners that stand still, chasers without a model do not") {
    SceneLoadOptions options;
    const std::vector<std::pair<std::string, std::string>> still = { { "num_worlds", "4" }, { "runners_still", "true" } };
    auto with = still;
    with.emplace_back("chaser", model);
    ScenarioRunner trained("pursuit_eval", with, options), untrained("pursuit_eval", still, options);
    const uint64_t caught = catches(trained, 600), by_chance = catches(untrained, 600);
    MESSAGE("600 windows in 4 worlds: " << caught << " catches with the policy, " << by_chance << " without");
    CHECK(caught >= 20);
    CHECK(caught > 4 * by_chance);
    // The agents' requests were answered: the systems cleared them as the host would
    for(auto [e, io] : trained.world(0).reg.view<AgentIO>().each()) {
        if(io.behavior == "Chaser") CHECK(std::abs(io.action[0]) + std::abs(io.action[1]) > 0.f);
    }
}

}
