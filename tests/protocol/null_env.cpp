#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <thread>

#include "scene/scenario_runner.h"
#include "support/mock_trainer.h"
#include "train/env_params_file.h"
#include "train/train_host.h"

using namespace dy;
using test::co::UnityInputProto;
using test::co::UnityOutputProto;

namespace {

const char* behavior = "HeidelBotBehavior";

UnityInputProto command(test::co::CommandProto c) {
    UnityInputProto input;
    input.mutable_rl_input()->set_command(c);
    return input;
}

std::vector<int> ids(const UnityOutputProto& out) {
    std::vector<int> v;
    for(auto& info : out.rl_output().agentinfos().at(behavior).value()) v.push_back(info.id());
    return v;
}

// Actions for the agents that asked for a decision, in output order (Python sends none for done agents)
UnityInputProto step(const UnityOutputProto& last) {
    UnityInputProto input = command(test::co::STEP);
    auto& list = (*input.mutable_rl_input()->mutable_agent_actions())[behavior];
    for(auto& info : last.rl_output().agentinfos().at(behavior).value()) {
        if(info.done()) continue;
        auto* action = list.add_value();
        for(int i = 0; i < 120; i++) action->add_continuous_actions(0.01f * i);
    }
    return input;
}

}

TEST_SUITE("protocol") {

TEST_CASE("P3.1 MockTrainer drives mode=null through init then empty then RESET then 100 STEP then QUIT") {

    test::MockTrainer trainer;
    REQUIRE(trainer.port() > 0);

    RunArgs args;
    args.mode = "null";
    TrainArgs train;
    train.port = trainer.port();
    SceneLoadOptions options;
    options.allow_renderer = false;
    ScenarioRunner runner("null", { { "episode_decisions", "30" } }, options);
    TrainHost host(args, train, runner);

    int code = -1;
    std::thread engine([&] { code = host.run(); });

    UnityInputProto init;
    auto* ii = init.mutable_rl_initialization_input();
    ii->set_seed(1234);
    ii->set_num_areas(3);
    ii->set_communication_version("1.5.0");
    ii->set_package_version("1.2.0.dev0");
    auto hello = trainer.initialize(init);
    REQUIRE(hello);
    const auto& io = hello->rl_initialization_output();
    CHECK(io.communication_version() == "1.5.0");
    CHECK(io.package_version() == "4.0.0");
    CHECK(io.capabilities().baserlcapabilities());
    CHECK(io.capabilities().hybridactions());

    // RESET: every agent, not done, with the brain parameters and the observation spec
    auto out = trainer.exchange(command(test::co::RESET));
    REQUIRE(out);
    const auto first_ids = ids(*out);
    CHECK(first_ids.size() == 25);
    REQUIRE(out->rl_initialization_output().brain_parameters_size() == 1);
    const auto& brain = out->rl_initialization_output().brain_parameters(0);
    CHECK(brain.brain_name() == behavior);
    CHECK(brain.is_training());
    CHECK(brain.action_spec().num_continuous_actions() == 120);
    CHECK(brain.action_spec().num_discrete_actions() == 0);
    const auto& info0 = out->rl_output().agentinfos().at(behavior).value(0);
    REQUIRE(info0.observations_size() == 1);
    CHECK(info0.observations(0).name() == "StackingSensor_size2_VectorSensor_size138");
    CHECK(info0.observations(0).shape_size() == 1);
    CHECK(info0.observations(0).shape(0) == 276);
    CHECK(info0.observations(0).float_data().data_size() == 276);
    CHECK(info0.observations(0).compression_type() == test::co::NONE);
    CHECK(!info0.done());
    CHECK(info0.reward() == 0.f);

    int terminal_outputs = 0, restarts = 0, resets = 0;
    for(int s = 1; s <= 100; s++) {
        UnityOutputProto last = *out;
        bool reset = s == 75;
        out = trainer.exchange(reset ? command(test::co::RESET) : step(last));
        REQUIRE(out);
        CHECK(ids(*out) == first_ids);
        CHECK(out->rl_initialization_output().brain_parameters_size() == 0);

        auto& infos = out->rl_output().agentinfos().at(behavior).value();
        auto& previous = last.rl_output().agentinfos().at(behavior).value();
        bool all_done = true;
        for(int i = 0; i < infos.size(); i++) {
            all_done = all_done && infos[i].done();
            if(infos[i].done()) CHECK(infos[i].max_step_reached());
            // A done agent got no action and is back, same id, in the next output
            if(previous[i].done()) {
                CHECK(!infos[i].done());
                restarts++;
            }
            if(reset) CHECK(!infos[i].done());
        }
        if(reset) resets++;
        if(all_done) terminal_outputs++;
    }
    // Episodes of 30 decisions: done at decisions 30 and 61 (the restart costs one window), reset at 75
    CHECK(terminal_outputs == 2);
    CHECK(restarts == 2 * 25);
    CHECK(resets == 1);

    trainer.send(command(test::co::QUIT));
    engine.join();
    CHECK(code == 0);
    CHECK(host.baseSeed() == 1234);

}

TEST_CASE("Null environment: header 400 from the trainer ends the run with exit 0") {

    test::MockTrainer trainer;
    RunArgs args;
    args.mode = "null";
    TrainArgs train;
    train.port = trainer.port();
    SceneLoadOptions options;
    options.allow_renderer = false;
    ScenarioRunner runner("null", {}, options);
    TrainHost host(args, train, runner);
    int code = -1;
    std::thread engine([&] { code = host.run(); });

    UnityInputProto init;
    init.mutable_rl_initialization_input()->set_seed(1);
    REQUIRE(trainer.initialize(init));
    REQUIRE(trainer.exchange(command(test::co::RESET)));
    trainer.close();
    engine.join();
    CHECK(code == 0);

}

TEST_CASE("P3.3 launch flags: Unity-style trainer flags and -set:x=1 parse and -envParams=missing exits 1") {

    auto t = parseTrainArgs({ "-nographics", "-batchmode", "--mlagents-port", "5005", "-logFile", "results/run/run_logs/Player-0.log",
        "-envParamsOut=results/run/envparams.txt", "-gitSha=1e1986303+dirty", "-set:x=1", "-set:energyScale=1200.5",
        "-envParams=replay.txt", "scene=null", "mode=null", "threads=2" });
    CHECK(t.errors.empty());
    CHECK(t.port == 5005);
    CHECK(t.log_file == "results/run/run_logs/Player-0.log");
    CHECK(t.env_params_out == "results/run/envparams.txt");
    CHECK(t.env_params_in == "replay.txt");
    CHECK(t.git_sha == "1e1986303+dirty");
    REQUIRE(t.set.size() == 2);
    CHECK(t.set[0] == std::pair<std::string, std::string>("x", "1"));
    CHECK(t.set[1] == std::pair<std::string, std::string>("energyScale", "1200.5"));
    CHECK(t.rest == std::vector<std::string> { "scene=null", "mode=null", "threads=2" });

    CHECK(!parseTrainArgs({ "--mlagents-port" }).errors.empty());
    CHECK(!parseTrainArgs({ "--mlagents-port", "abc" }).errors.empty());
    CHECK(!parseTrainArgs({ "-set:novalue" }).errors.empty());
    CHECK(!parseTrainArgs({ "-unknownFlag" }).errors.empty());

    // A missing replay file is fatal before any connection, as in Unity (EnvParams.cs Fail)
    RunArgs args;
    args.mode = "null";
    TrainArgs missing = parseTrainArgs({ "--mlagents-port", "5005", "-envParams=tests/data/does_not_exist.txt" });
    SceneLoadOptions options;
    options.allow_renderer = false;
    ScenarioRunner runner("null", {}, options);
    CHECK(TrainHost(args, missing, runner).run() == 1);

    TrainArgs bad = parseTrainArgs({ "--mlagents-port", "5005", "-set:x=abc" });
    CHECK(TrainHost(args, bad, runner).run() == 1);

}

TEST_CASE("P3.5 envparams.txt has the header and ordinal order and round-trip floats and sha") {

    auto dir = std::filesystem::temp_directory_path() / "dynamical_tests" / "myrun";
    std::filesystem::remove_all(dir);
    EnvParamsFile file;
    file.behavior = "HeidelBotBehavior";
    file.written = 4200;
    file.header["sha"] = "abc123def";
    file.values = { { "b", 1.5f }, { "a", 0.1f }, { "B", 2.f }, { "pi", 3.14159274f }, { "tiny", 1.17549435e-38f }, { "speed", 1.36f } };
    REQUIRE(writeEnvParamsFile(dir / "envparams.txt", file));

    std::ifstream in(dir / "envparams.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), {});
    CHECK(content.find("# run=myrun behavior=HeidelBotBehavior written=4200 sha=abc123def\n") == 0);
    CHECK(content.find('\r') == std::string::npos);

    // Ordinal: upper case before lower case
    std::vector<std::string> names;
    std::istringstream lines(content);
    std::string line;
    std::getline(lines, line);
    while(std::getline(lines, line)) names.push_back(line.substr(0, line.find(' ')));
    CHECK(names == std::vector<std::string> { "B", "a", "b", "pi", "speed", "tiny" });

    // Our reader (Unity's format) gives the same floats back, bit for bit
    EnvParamStore store;
    std::string error;
    REQUIRE(store.loadFile((dir / "envparams.txt").string(), error));
    auto values = store.resolved();
    for(auto& [k, v] : file.values) CHECK(values.at(k) == v);
    in.close();
    std::error_code ec;
    std::filesystem::remove_all(dir.parent_path(), ec);

}

TEST_CASE("Env params: -set beats the live value which beats the replay file") {
    auto path = std::filesystem::temp_directory_path() / "dynamical_tests_replay.txt";
    {
        std::ofstream out(path);
        out << "# run=x behavior=HeidelBotBehavior written=0\n  targetSpeed\t1.25\nenergyScale 900\npushForce 10\n";
    }
    EnvParamStore store;
    std::string error;
    REQUIRE(store.loadFile(path.string(), error));
    store.setLive("energyScale", 1000.f);
    store.setLive("pushForce", 20.f);
    REQUIRE(store.setOverride("pushForce", "30", error));
    auto r = store.resolved();
    CHECK(r.at("targetSpeed") == 1.25f);
    CHECK(r.at("energyScale") == 1000.f);
    CHECK(r.at("pushForce") == 30.f);
    std::filesystem::remove(path);
}

}
