#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <thread>

#include "mlagents/side_channels.h"
#include "scene/scenario_runner.h"
#include "support/mock_trainer.h"
#include "train/train_host.h"
#include "train/worlds_env.h"
#include "util/paths.h"

using namespace dy;
using test::co::UnityInputProto;
using test::co::UnityOutputProto;

namespace {

UnityInputProto command(test::co::CommandProto c) {
    UnityInputProto input;
    input.mutable_rl_input()->set_command(c);
    return input;
}

// What the Python environment sends: for every behaviour of the last output, an action per agent that did not end,
// in the output's order; nothing for a behaviour that was not in it. `drop`: one action too few for that behaviour
UnityInputProto step(const UnityOutputProto& last, int s, const std::string& drop = {}) {
    UnityInputProto input = command(test::co::STEP);
    for(auto& [name, infos] : last.rl_output().agentinfos()) {
        int wanted = 0;
        for(auto& info : infos.value()) wanted += info.done() ? 0 : 1;
        if(wanted == 0) continue;
        if(name == drop) wanted--;
        auto& list = (*input.mutable_rl_input()->mutable_agent_actions())[name];
        int k = 0;
        for(auto& info : infos.value()) {
            if(info.done() || k++ >= wanted) continue;
            auto* action = list.add_value();
            action->add_continuous_actions(1.f);
            action->add_continuous_actions(std::sin(0.3f * s + info.id()));
            action->add_discrete_actions((s + info.id()) % 2);
        }
    }
    return input;
}

struct Engine {
    test::MockTrainer trainer;
    RunArgs args;
    TrainArgs train;
    SceneLoadOptions options;
    std::unique_ptr<ScenarioRunner> runner;
    std::unique_ptr<TrainHost> host;
    std::thread thread;
    int code = -1;

    Engine(const std::string& scene, std::vector<std::pair<std::string, std::string>> vars) {
        REQUIRE(trainer.port() > 0);
        args.mode = "train";
        args.threads = 2;
        train.port = trainer.port();
        options.allow_renderer = false;
        runner = std::make_unique<ScenarioRunner>(scene, vars, options);
        host = std::make_unique<TrainHost>(args, train, *runner);
        thread = std::thread([this] { code = host->run(); });
        UnityInputProto init;
        init.mutable_rl_initialization_input()->set_seed(7);
        init.mutable_rl_initialization_input()->set_communication_version("1.5.0");
        REQUIRE(trainer.initialize(init));
    }

    // A test that gave up before its QUIT
    ~Engine() {
        if(!thread.joinable()) return;
        trainer.close();
        thread.join();
    }
};

}

TEST_SUITE("protocol") {

TEST_CASE("MA.1 MockTrainer drives mode=train on pursuit: two behaviours, agents that end and are born, runners at half the rate") {

    // A small field and a wide reach: runners are caught all the time
    Engine engine("pursuit", { { "num_worlds", "3" }, { "field", "5" }, { "catch_radius", "2" }, { "chaser_decisions", "40" } });
    auto& trainer = engine.trainer;

    auto out = trainer.exchange(command(test::co::RESET));
    REQUIRE(out);

    // Both behaviours start with the reset: their brain parameters come with it, once
    REQUIRE(out->rl_initialization_output().brain_parameters_size() == 2);
    std::set<std::string> brains;
    for(auto& brain : out->rl_initialization_output().brain_parameters()) {
        brains.insert(brain.brain_name());
        CHECK(brain.action_spec().num_continuous_actions() == 2);
        CHECK(brain.action_spec().num_discrete_actions() == 1);
        REQUIRE(brain.action_spec().discrete_branch_sizes_size() == 1);
        CHECK(brain.action_spec().discrete_branch_sizes(0) == 2);
    }
    CHECK(brains == std::set<std::string> { "Chaser", "Runner" });

    const auto& first = out->rl_output().agentinfos();
    REQUIRE(first.count("Chaser") == 1);
    REQUIRE(first.count("Runner") == 1);
    CHECK(first.at("Chaser").value_size() == 3 * 2);
    CHECK(first.at("Runner").value_size() == 3 * 4);

    std::set<int> chasers, ended;
    std::map<int, int> runner_steps;        // id -> infos seen
    for(auto& [name, infos] : first) {
        for(auto& info : infos.value()) {
            CHECK(!info.done());
            CHECK(info.reward() == 0.f);
            // A world's own agent would have its world's index: components come after
            CHECK(info.id() >= 3);
            REQUIRE(info.observations_size() == 2);
            const auto& vector = info.observations(0);
            CHECK(vector.name() == "VectorSensor_size6");
            REQUIRE(vector.shape_size() == 1);
            CHECK(vector.shape(0) == 6);
            CHECK(vector.float_data().data_size() == 6);
            const auto& buffer = info.observations(1);
            CHECK(buffer.name() == "Others");
            REQUIRE(buffer.shape_size() == 2);
            CHECK(buffer.shape(0) == 8);
            CHECK(buffer.shape(1) == 6);
            CHECK(buffer.float_data().data_size() == 48);
            // Variable size along the rows, none along the values
            REQUIRE(buffer.dimension_properties_size() == 2);
            CHECK(buffer.dimension_properties(0) == 4);
            CHECK(buffer.dimension_properties(1) == 1);
            if(name == "Chaser") chasers.insert(info.id());
            else runner_steps[info.id()]++;
        }
    }
    CHECK(chasers.size() == 6);
    CHECK(runner_steps.size() == 12);
    for(int id : chasers) CHECK(runner_steps.count(id) == 0);

    const int steps = 200;
    int terminal_runners = 0, caught = 0, terminal_chasers = 0, runner_outputs = 0, stat_outputs = 0;
    double chaser_reward = 0.;
    for(int s = 1; s <= steps; s++) {
        UnityOutputProto last = *out;
        out = trainer.exchange(step(last, s));
        REQUIRE(out);
        CHECK(out->rl_initialization_output().brain_parameters_size() == 0);

        const auto& infos = out->rl_output().agentinfos();
        // Chasers decide every window, all of them, in the same order
        REQUIRE(infos.count("Chaser") == 1);
        const auto& list = infos.at("Chaser").value();
        REQUIRE(list.size() == 6);
        const auto& previous = last.rl_output().agentinfos().at("Chaser").value();
        for(int i = 0; i < 6; i++) {
            CHECK(chasers.count(list[i].id()) == 1);
            CHECK(list[i].id() == previous[i].id());
            chaser_reward += list[i].reward();
            if(list[i].done()) {
                terminal_chasers++;
                CHECK(list[i].max_step_reached());
            }
            // The same id starts its next episode
            if(previous[i].done()) {
                CHECK(!list[i].done());
                CHECK(list[i].reward() == 0.f);
            }
        }

        if(infos.count("Runner")) {
            const auto& runners = infos.at("Runner").value();
            runner_outputs += runners.empty() ? 0 : 1;
            // Never all twelve at once after the reset: they decide every second window, in two halves
            for(auto& info : runners) {
                // An agent that ended does not come back
                CHECK(ended.count(info.id()) == 0);
                runner_steps[info.id()]++;
                if(info.done()) {
                    ended.insert(info.id());
                    terminal_runners++;
                    if(!info.max_step_reached()) {
                        caught++;
                        CHECK(info.reward() <= -0.99f);
                    }
                }
            }
        }

        std::vector<mlagents::ChannelMessage> messages;
        REQUIRE(mlagents::readChannelMessages(out->rl_output().side_channel(), messages));
        stat_outputs += messages.empty() ? 0 : 1;
    }

    CHECK(caught > 20);
    CHECK(terminal_runners >= caught);
    // Every runner that ended was replaced by one with another id
    CHECK((int) runner_steps.size() >= 12 + terminal_runners - 12);
    CHECK((int) runner_steps.size() <= 12 + terminal_runners);
    // What the chasers were paid is what the runners lost, but for the catches of the last windows
    CHECK(chaser_reward <= caught + 12.);
    CHECK(chaser_reward >= caught - 6.);
    // 40-decision episodes
    CHECK(terminal_chasers >= 6 * 4);
    CHECK(runner_outputs > steps / 2);
    // Chasers report every decision: all windows but those where all six start an episode
    CHECK(stat_outputs >= steps - steps / 40 - 1);
    // A runner asks every second window: one that lived through the run was seen about half the time
    for(auto& [id, n] : runner_steps) CHECK(n <= steps / 2 + 2);

    trainer.send(command(test::co::QUIT));
    engine.thread.join();
    CHECK(engine.code == 0);

}

TEST_CASE("MA.2 a second RESET starts every agent again with new ids for the components") {
    Engine engine("pursuit", { { "num_worlds", "2" } });
    auto& trainer = engine.trainer;
    auto out = trainer.exchange(command(test::co::RESET));
    REQUIRE(out);
    std::set<int> before;
    for(auto& [name, infos] : out->rl_output().agentinfos()) for(auto& info : infos.value()) before.insert(info.id());
    CHECK(before.size() == 12);
    for(int s = 1; s <= 5; s++) {
        out = trainer.exchange(step(*out, s));
        REQUIRE(out);
    }
    out = trainer.exchange(command(test::co::RESET));
    REQUIRE(out);
    CHECK(out->rl_initialization_output().brain_parameters_size() == 0);
    int count = 0;
    for(auto& [name, infos] : out->rl_output().agentinfos()) {
        for(auto& info : infos.value()) {
            count++;
            CHECK(before.count(info.id()) == 0);
            CHECK(!info.done());
        }
    }
    CHECK(count == 12);
    out = trainer.exchange(step(*out, 6));
    REQUIRE(out);
    trainer.send(command(test::co::QUIT));
    engine.thread.join();
    CHECK(engine.code == 0);
}

TEST_CASE("MA.3 an action missing for one behaviour stops the host with the behaviour's name") {
    Engine engine("pursuit", { { "num_worlds", "1" } });
    auto& trainer = engine.trainer;
    auto out = trainer.exchange(command(test::co::RESET));
    REQUIRE(out);
    trainer.send(step(*out, 1, "Runner"));
    engine.thread.join();
    CHECK(engine.code == 2);
}

TEST_CASE("MA.5 MockTrainer drives mode=train on sandbox_train: four behaviours, walkers every window, minds every sixth") {

    if(!std::filesystem::exists(libraryResourceFile("animals/bodies/deer_red.body.json")) || !std::filesystem::exists(libraryResourceFile("animals/bodies/wolf_2.body.json"))) return;
    Engine engine("sandbox_train", { { "num_worlds", "2" } });
    auto& trainer = engine.trainer;
    auto out = trainer.exchange(command(test::co::RESET));
    REQUIRE(out);

    std::map<std::string, const test::co::BrainParametersProto*> brains;
    for(auto& brain : out->rl_initialization_output().brain_parameters()) brains[brain.brain_name()] = &brain;
    REQUIRE(brains.size() == 4);
    REQUIRE(brains.count("Animal_deer_red") == 1);
    REQUIRE(brains.count("Action_wolf_2") == 1);
    CHECK(brains["Animal_deer_red"]->action_spec().num_continuous_actions() == 8 * 21);
    CHECK(brains["Animal_deer_red"]->action_spec().num_discrete_actions() == 0);
    CHECK(brains["Animal_wolf_2"]->action_spec().num_continuous_actions() == 8 * 22);
    CHECK(brains["Action_deer_red"]->action_spec().num_continuous_actions() == 4);
    REQUIRE(brains["Action_deer_red"]->action_spec().discrete_branch_sizes_size() == 1);
    CHECK(brains["Action_deer_red"]->action_spec().discrete_branch_sizes(0) == 5);

    const auto& first = out->rl_output().agentinfos();
    CHECK(first.at("Animal_deer_red").value_size() == 2 * 12);
    CHECK(first.at("Action_deer_red").value_size() == 2 * 12);
    CHECK(first.at("Animal_wolf_2").value_size() == 2 * 4);
    CHECK(first.at("Action_wolf_2").value_size() == 2 * 4);
    {
        const auto& walker = first.at("Animal_deer_red").value(0);
        REQUIRE(walker.observations_size() == 2);
        CHECK(walker.observations(0).shape(0) == 2 * (7 + 9 * 22));
        CHECK(walker.observations(1).name() == "nogail_order");
        CHECK(walker.observations(1).shape(0) == 7);
        const auto& mind = first.at("Action_wolf_2").value(0);
        REQUIRE(mind.observations_size() == 2);
        CHECK(mind.observations(0).shape(0) == 39);
        CHECK(mind.observations(1).name() == "Interests");
        REQUIRE(mind.observations(1).shape_size() == 2);
        CHECK(mind.observations(1).shape(0) == 16);
        CHECK(mind.observations(1).shape(1) == 12);
    }

    // What the trainer would send: zeros for the walkers (they stand), a mode and a speed for the minds
    auto sandboxStep = [](const UnityOutputProto& last, int s) {
        UnityInputProto input = command(test::co::STEP);
        for(auto& [name, infos] : last.rl_output().agentinfos()) {
            const bool mind = name.rfind("Action_", 0) == 0;
            const int actions = name == "Animal_deer_red" ? 8 * 21 : name == "Animal_wolf_2" ? 8 * 22 : 4;
            for(auto& info : infos.value()) {
                if(info.done()) continue;
                auto* action = (*input.mutable_rl_input()->mutable_agent_actions())[name].add_value();
                for(int i = 0; i < actions; i++) action->add_continuous_actions(0.f);
                if(mind) action->add_discrete_actions((s + info.id()) % 5);
            }
        }
        return input;
    };

    const int steps = 60;
    int mind_infos = 0, walker_infos = 0, outputs_with_minds = 0;
    for(int s = 1; s <= steps; s++) {
        out = trainer.exchange(sandboxStep(*out, s));
        REQUIRE(out);
        CHECK(out->rl_initialization_output().brain_parameters_size() == 0);
        const auto& infos = out->rl_output().agentinfos();
        // Every walker, every window (nobody falls or dies standing for two seconds)
        REQUIRE(infos.count("Animal_deer_red") == 1);
        CHECK(infos.at("Animal_deer_red").value_size() == 24);
        CHECK(infos.at("Animal_wolf_2").value_size() == 8);
        walker_infos += infos.at("Animal_deer_red").value_size() + infos.at("Animal_wolf_2").value_size();
        int minds = 0;
        for(const char* name : { "Action_deer_red", "Action_wolf_2" }) if(infos.count(name)) minds += infos.at(name).value_size();
        mind_infos += minds;
        outputs_with_minds += minds > 0 ? 1 : 0;
        for(auto& [name, list] : infos) for(auto& info : list.value()) CHECK(std::isfinite(info.reward()));
    }
    CHECK(walker_infos == steps * 32);
    // 32 minds, each a decision in six windows
    CHECK(mind_infos == steps / 6 * 32);
    CHECK(outputs_with_minds > steps / 2);

    trainer.send(command(test::co::QUIT));
    engine.thread.join();
    CHECK(engine.code == 0);

}

TEST_CASE("MA.4 the check and the description of a scene's agents") {
    SceneLoadOptions options;
    options.allow_renderer = false;
    {
        ScenarioRunner runner("pursuit", { { "num_worlds", "2" } }, options);
        CHECK(WorldsEnvironment::check(runner).empty());
        CHECK(WorldsEnvironment::behaviors(runner) == std::vector<std::string> { "Chaser", "Runner" });
        WorldsEnvironment env(runner, 1);
        CHECK(env.describe() == "4 Chaser (6 + 8x6 observations, 2 + [2] actions), 8 Runner (6 + 8x6 observations, 2 + [2] actions)");
    }
    {
        ScenarioRunner runner("sphere_roll", { { "num_worlds", "2" } }, options);
        CHECK(WorldsEnvironment::check(runner).empty());
        CHECK(WorldsEnvironment::behaviors(runner) == std::vector<std::string> { "SphereRoller" });
        WorldsEnvironment env(runner, 1);
        CHECK(env.describe() == "2 SphereRoller (16 observations, 3 actions)");
    }
    {
        ScenarioRunner runner("test_boxes", { { "num_worlds", "1" } }, options);
        CHECK(WorldsEnvironment::check(runner) == "world 0 has no agent");
    }
}

}
