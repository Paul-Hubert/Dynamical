#include <doctest/doctest.h>

#include <thread>

#include "mlagents/side_channels.h"
#include "scene/scenario_runner.h"
#include "support/mock_trainer.h"
#include "train/train_host.h"

using namespace dy;
using test::co::UnityInputProto;
using test::co::UnityOutputProto;

namespace {

const char* behavior = "SphereRoller";

UnityInputProto command(test::co::CommandProto c) {
    UnityInputProto input;
    input.mutable_rl_input()->set_command(c);
    return input;
}

UnityInputProto step(const UnityOutputProto& last, int s) {
    UnityInputProto input = command(test::co::STEP);
    auto& list = (*input.mutable_rl_input()->mutable_agent_actions())[behavior];
    for(auto& info : last.rl_output().agentinfos().at(behavior).value()) {
        if(info.done()) continue;
        auto* action = list.add_value();
        for(int i = 0; i < 3; i++) action->add_continuous_actions(std::sin(0.1f * s + i + info.id()));
    }
    return input;
}

}

TEST_SUITE("protocol") {

TEST_CASE("SR.9 MockTrainer drives mode=train on sphere_roll: RESET then 60 STEP then QUIT") {

    test::MockTrainer trainer;
    REQUIRE(trainer.port() > 0);

    RunArgs args;
    args.mode = "train";
    args.threads = 2;
    TrainArgs train;
    train.port = trainer.port();
    SceneLoadOptions options;
    options.allow_renderer = false;
    ScenarioRunner runner("sphere_roll", { { "num_worlds", "4" }, { "max_decisions", "25" } }, options);
    TrainHost host(args, train, runner);

    int code = -1;
    std::thread engine([&] { code = host.run(); });

    UnityInputProto init;
    init.mutable_rl_initialization_input()->set_seed(99);
    init.mutable_rl_initialization_input()->set_communication_version("1.5.0");
    REQUIRE(trainer.initialize(init));

    auto out = trainer.exchange(command(test::co::RESET));
    REQUIRE(out);
    REQUIRE(out->rl_initialization_output().brain_parameters_size() == 1);
    const auto& brain = out->rl_initialization_output().brain_parameters(0);
    CHECK(brain.brain_name() == behavior);
    CHECK(brain.action_spec().num_continuous_actions() == 3);
    CHECK(brain.action_spec().num_discrete_actions() == 0);
    const auto& infos0 = out->rl_output().agentinfos().at(behavior).value();
    REQUIRE(infos0.size() == 4);
    for(int i = 0; i < 4; i++) {
        CHECK(infos0[i].id() == i);
        CHECK(!infos0[i].done());
        REQUIRE(infos0[i].observations_size() == 1);
        CHECK(infos0[i].observations(0).name() == "VectorSensor_size16");
        CHECK(infos0[i].observations(0).shape(0) == 16);
        CHECK(infos0[i].observations(0).float_data().data_size() == 16);
    }

    int terminal = 0, restarts = 0, stat_outputs = 0, all_restarting = 0;
    for(int s = 1; s <= 60; s++) {
        UnityOutputProto last = *out;
        out = trainer.exchange(step(last, s));
        REQUIRE(out);
        auto& infos = out->rl_output().agentinfos().at(behavior).value();
        auto& previous = last.rl_output().agentinfos().at(behavior).value();
        REQUIRE(infos.size() == 4);
        bool every_restart = true;
        for(int i = 0; i < 4; i++) every_restart = every_restart && previous[i].done();
        all_restarting += every_restart ? 1 : 0;
        for(int i = 0; i < 4; i++) {
            CHECK(infos[i].id() == i);
            CHECK(infos[i].reward() >= 0.f);
            CHECK(infos[i].reward() <= 1.f);
            if(infos[i].done()) {
                terminal++;
                CHECK(infos[i].max_step_reached());
            }
            if(previous[i].done()) {
                CHECK(!infos[i].done());
                CHECK(infos[i].reward() == 0.f);
                restarts++;
            }
        }
        std::vector<mlagents::ChannelMessage> messages;
        REQUIRE(mlagents::readChannelMessages(out->rl_output().side_channel(), messages));
        if(!messages.empty()) {
            stat_outputs++;
            for(auto& m : messages) CHECK(m.channel == mlagents::statsChannel());
        }
    }
    // 25-decision episodes: done at decisions 25 and 51, unless an agent rolls off the field first
    CHECK(terminal >= 8);
    CHECK(restarts == terminal);
    // Stats in every window but those where every agent starts its episode (no decision to rate yet)
    CHECK(stat_outputs == 60 - all_restarting);

    trainer.send(command(test::co::QUIT));
    engine.join();
    CHECK(code == 0);
    CHECK(host.baseSeed() == 99);

}

TEST_CASE("SR.10 mode=train refuses a scene without agent worlds") {
    RunArgs args;
    args.mode = "train";
    TrainArgs train;
    train.port = 5999;
    SceneLoadOptions options;
    options.allow_renderer = false;
    ScenarioRunner runner("test_boxes", { { "num_worlds", "1" } }, options);
    CHECK(TrainHost(args, train, runner).run() == 1);
}

}
