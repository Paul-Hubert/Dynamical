#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "logic/agents/agent_io.h"
#include "logic/agents/pursuit.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

namespace {

ScenarioRunner pursuitRunner(int worlds, std::vector<std::pair<std::string, std::string>> vars = {}) {
    vars.emplace_back("num_worlds", std::to_string(worlds));
    return ScenarioRunner("pursuit", vars, SceneLoadOptions {});
}

std::vector<entt::entity> agents(World& w) {
    std::vector<entt::entity> list;
    for(auto e : w.reg.view<PursuitDot, AgentIO>()) list.push_back(e);
    std::sort(list.begin(), list.end());
    return list;
}

// What the training host does between two windows: the agents that asked get an action, the flag is cleared
enum class Drive { Still, Heuristic, ChasersOnly };

struct Window {
    int chasers = 0, runners = 0;       // that asked for a decision or ended
    int caught = 0, old = 0;
    double chaser_reward = 0., runner_reward = 0.;
};

Window window(ScenarioRunner& runner, Drive drive) {
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& w = runner.world(i);
        for(auto e : agents(w)) {
            auto& io = w.reg.get<AgentIO>(e);
            const auto& dot = w.reg.get<PursuitDot>(e);
            if(!io.requested || io.done) {
                io.requested = false;
                continue;
            }
            io.requested = false;
            if(drive == Drive::Heuristic || (drive == Drive::ChasersOnly && dot.chaser)) {
                pursuitHeuristic(io, dot.chaser);
            } else {
                io.action = { -1.f, 0.f };
                io.discrete = { 0 };
            }
        }
    }
    runner.step(runner.world(0).reg.ctx().get<WorldClock>().decision_period);
    Window out;
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& w = runner.world(i);
        for(auto e : agents(w)) {
            const auto& io = w.reg.get<AgentIO>(e);
            const auto& dot = w.reg.get<PursuitDot>(e);
            if(!io.requested && !io.done) continue;
            // An ended agent that waits for the next window was counted with the window it ended in
            if(io.done && !io.requested) continue;
            (dot.chaser ? out.chasers : out.runners)++;
            (dot.chaser ? out.chaser_reward : out.runner_reward) += io.reward;
            if(io.done && !io.truncated) out.caught++;
            if(io.done && io.truncated) out.old++;
        }
    }
    return out;
}

// RESET: a fresh start of every agent without stepping
void resetAll(ScenarioRunner& runner) {
    for(size_t i = 0; i < runner.worldCount(); i++) {
        runner.world(i).reg.ctx().get<AgentsReset>().requested = true;
        runner.world(i).systems.fixed_observe_only();
    }
}

}

TEST_SUITE("scenario") {

TEST_CASE("PURSUIT.1 a world holds chasers and runners as agent components of two behaviours with a buffer and a branch") {
    auto runner = pursuitRunner(2, { { "chasers", "3" }, { "runners", "5" } });
    for(size_t i = 0; i < 2; i++) {
        auto& w = runner.world(i);
        CHECK(w.reg.ctx().find<AgentIO>() == nullptr);
        REQUIRE(w.reg.ctx().find<AgentsReset>() != nullptr);
        std::map<std::string, int> count;
        for(auto e : agents(w)) {
            const auto& io = w.reg.get<AgentIO>(e);
            count[io.behavior]++;
            CHECK(io.behavior == (w.reg.get<PursuitDot>(e).chaser ? "Chaser" : "Runner"));
            CHECK(io.observation_size == PursuitObs::size);
            CHECK(io.action_size == pursuit_actions);
            REQUIRE(io.branches.size() == 1);
            CHECK(io.branches[0] == 2);
            REQUIRE(io.sensors.size() == 1);
            CHECK(io.sensors[0].rows == 8);
            CHECK(io.sensors[0].size == PursuitObs::row);
            CHECK(io.id == -1);
        }
        CHECK(count["Chaser"] == 3);
        CHECK(count["Runner"] == 5);
    }
}

TEST_CASE("PURSUIT.2 every agent asks after a reset then chasers every window and runners every second one") {
    auto runner = pursuitRunner(3, { { "catch_radius", "0.0001" } });
    resetAll(runner);
    int asked = 0;
    for(size_t i = 0; i < 3; i++) {
        for(auto e : agents(runner.world(i))) {
            const auto& io = runner.world(i).reg.get<AgentIO>(e);
            asked += io.requested ? 1 : 0;
            CHECK(io.reward == 0.f);
            CHECK(!io.done);
        }
    }
    CHECK(asked == 3 * 6);
    // Nobody moves: no catch, so every runner asks exactly once in two windows
    for(int k = 0; k < 6; k++) {
        Window a = window(runner, Drive::Still), b = window(runner, Drive::Still);
        CHECK(a.chasers == 3 * 2);
        CHECK(b.chasers == 3 * 2);
        CHECK(a.runners + b.runners == 3 * 4);
        CHECK(a.caught + b.caught == 0);
        // The runner's reward for a decision it is not caught in
        CHECK(a.runner_reward + b.runner_reward == doctest::Approx(3 * 4 * 0.01));
        CHECK(a.chaser_reward + b.chaser_reward == 0.);
    }
}

TEST_CASE("PURSUIT.3 a caught runner ends with -1 and leaves and another is born and its chaser gets +1") {
    auto runner = pursuitRunner(4);
    resetAll(runner);
    std::set<entt::entity> first;
    for(auto e : agents(runner.world(0))) first.insert(e);

    int caught = 0;
    double chaser_reward = 0., runner_reward = 0.;
    int runner_decisions = 0;
    for(int k = 0; k < 600; k++) {
        Window w = window(runner, Drive::ChasersOnly);
        caught += w.caught;
        chaser_reward += w.chaser_reward;
        runner_reward += w.runner_reward;
        runner_decisions += w.runners - w.caught;
        for(size_t i = 0; i < 4; i++) {
            int chasers = 0, runners = 0;
            for(auto e : agents(runner.world(i))) (runner.world(i).reg.get<PursuitDot>(e).chaser ? chasers : runners)++;
            CHECK(chasers == 2);
            CHECK(runners == 4);
        }
    }
    // Chasers that head for the nearest runner catch runners that stand still
    CHECK(caught >= 4 * 4);
    uint64_t catches = 0, births = 0;
    for(size_t i = 0; i < 4; i++) {
        const auto& field = runner.world(i).reg.ctx().get<PursuitField>();
        catches += field.catches;
        births += field.births;
    }
    // But for the last ones: a runner caught in its first window ends with the next, a chaser that catches in the
    // first window of its episode is paid with its first decision, a runner that ended is replaced a window later
    CHECK(caught <= (int) catches);
    CHECK(caught + 4 >= (int) catches);
    CHECK(chaser_reward <= (double) catches + 1e-6);
    CHECK(chaser_reward + 4. >= (double) catches);
    CHECK(births + 4 * 4 >= catches);
    // Every runner step that is neither a catch nor the first of a life pays the reward for staying alive
    CHECK(runner_reward == doctest::Approx(-(double) caught + 0.01 * (runner_decisions - (double) births)).epsilon(0.01));
    // New runners are new entities with no id yet (the host gives one)
    int fresh = 0;
    for(auto e : agents(runner.world(0))) fresh += first.count(e) ? 0 : 1;
    CHECK(fresh > 0);
}

TEST_CASE("PURSUIT.4 a runner ends of old age and leaves and a chaser's episode ends by time and it starts again in place") {
    auto runner = pursuitRunner(1, { { "runner_decisions", "5" }, { "chaser_decisions", "7" }, { "catch_radius", "0.0001" } });
    resetAll(runner);
    auto& reg = runner.world(0).reg;
    std::vector<entt::entity> chasers;
    for(auto e : agents(runner.world(0))) if(reg.get<PursuitDot>(e).chaser) chasers.push_back(e);

    // Seven windows: the chasers' seventh decision ends their episode, by time
    for(int k = 0; k < 6; k++) CHECK(window(runner, Drive::Still).old == 0);
    Window seventh = window(runner, Drive::Still);
    for(auto e : chasers) {
        REQUIRE(reg.valid(e));
        CHECK(reg.get<AgentIO>(e).done);
        CHECK(reg.get<AgentIO>(e).truncated);
    }
    CHECK(seventh.old >= 2);
    // The next window they are the same entities at the start of an episode
    window(runner, Drive::Still);
    for(auto e : chasers) {
        REQUIRE(reg.valid(e));
        const auto& io = reg.get<AgentIO>(e);
        CHECK(!io.done);
        CHECK(io.requested);
        CHECK(io.reward == 0.f);
        CHECK(reg.get<PursuitDot>(e).decisions == 0);
    }
    // Runners: five decisions of their own, ten or eleven windows, then they are replaced
    int old = seventh.old - 2;
    for(int k = 0; k < 8; k++) old += window(runner, Drive::Still).old;
    CHECK(old >= 4 + 2);    // the four runners, and the chasers' second episode
    CHECK(reg.ctx().get<PursuitField>().births >= 4);
    CHECK(reg.ctx().get<PursuitField>().catches == 0);
}

TEST_CASE("PURSUIT.5 the buffer lists the others nearest first in the dot's own frame") {
    auto runner = pursuitRunner(1, { { "chasers", "1" }, { "runners", "2" } });
    resetAll(runner);
    auto& reg = runner.world(0).reg;
    entt::entity chaser = entt::null;
    std::vector<entt::entity> runners;
    for(auto e : agents(runner.world(0))) {
        if(reg.get<PursuitDot>(e).chaser) chaser = e;
        else runners.push_back(e);
    }
    REQUIRE(runners.size() == 2);
    // The chaser at the centre looking down -z; a runner 5 m ahead, the other 4 m to its left (-x)
    auto set = [&](entt::entity e, glm::vec2 p) {
        auto& dot = reg.get<PursuitDot>(e);
        dot.position = p;
        dot.yaw = 0.f;
        dot.velocity = glm::vec2(0.f);
    };
    set(chaser, glm::vec2(0.f));
    set(runners[0], glm::vec2(0.f, -5.f));
    set(runners[1], glm::vec2(-4.f, 0.f));
    // Whatever their turn: a dot that has not been observed in its episode is
    for(auto e : runners) reg.get<PursuitDot>(e).fresh = true;
    runner.world(0).systems.fixed_observe_only();

    const auto& io = reg.get<AgentIO>(chaser);
    const auto& buffer = io.sensors[0];
    REQUIRE(buffer.count == 2);
    const float* near = buffer.data.data();
    const float* far = near + PursuitObs::row;
    CHECK(near[PursuitObs::other_position] == doctest::Approx(0.f));
    CHECK(near[PursuitObs::other_position + 1] == doctest::Approx(4.f / 40.f));
    CHECK(far[PursuitObs::other_position] == doctest::Approx(5.f / 40.f));
    CHECK(far[PursuitObs::other_position + 1] == doctest::Approx(0.f));
    CHECK(near[PursuitObs::other_runner] == 1.f);
    CHECK(near[PursuitObs::other_chaser] == 0.f);
    // The rows after the last one are zero
    for(size_t i = 2 * PursuitObs::row; i < buffer.data.size(); i++) CHECK(buffer.data[i] == 0.f);
    CHECK(io.observation[PursuitObs::heading] == doctest::Approx(0.f));
    CHECK(io.observation[PursuitObs::heading + 1] == doctest::Approx(1.f));

    // The heuristic turns the chaser to its left, toward the nearest runner, and the runner there away from it
    AgentIO copy = io;
    pursuitHeuristic(copy, true);
    CHECK(copy.action[0] == 1.f);
    CHECK(copy.action[1] == 1.f);
    CHECK(copy.discrete[0] == 1);
    AgentIO fled = reg.get<AgentIO>(runners[0]);
    pursuitHeuristic(fled, false);
    CHECK(fled.action[0] == 1.f);
    // Its chaser is straight behind (it looks down -z from 5 m ahead): keep going
    CHECK(std::abs(fled.action[1]) < 1e-4f);
}

TEST_CASE("PURSUIT.6 two runs of the same seed are the same") {
    auto a = pursuitRunner(2), b = pursuitRunner(2);
    resetAll(a);
    resetAll(b);
    for(int k = 0; k < 150; k++) {
        window(a, Drive::Heuristic);
        window(b, Drive::Heuristic);
    }
    for(size_t i = 0; i < 2; i++) {
        auto la = agents(a.world(i)), lb = agents(b.world(i));
        REQUIRE(la.size() == lb.size());
        for(size_t k = 0; k < la.size(); k++) {
            const auto& da = a.world(i).reg.get<PursuitDot>(la[k]);
            const auto& db = b.world(i).reg.get<PursuitDot>(lb[k]);
            CHECK(da.position.x == db.position.x);
            CHECK(da.position.y == db.position.y);
            CHECK(da.chaser == db.chaser);
        }
        CHECK(a.world(i).reg.ctx().get<PursuitField>().catches == b.world(i).reg.ctx().get<PursuitField>().catches);
    }
}

}
