#include "train_host.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

#include "env_params_file.h"
#include "mlagents/client.h"
#include "mlagents/side_channels.h"
#include "null_env.h"
#include "process_control.h"
#include "util/log.h"

#include "dynamical_version.h"

using namespace dy;
namespace co = mlagents::co;

namespace {

int sceneInt(const SceneDef& scene, const char* name, int fallback) {
    const auto& vars = scene.resolved["variables"];
    if(vars.contains(name)) return vars[name]["default"].get<int>();
    return fallback;
}

}

int TrainHost::run() {

    if(!loadEnvParams()) return 1;

    std::string mode = args.mode.empty() ? "train" : args.mode;
    if(mode == "bench") return bench();
    if(mode == "null") return null();

    dy::log(dy::error) << "mode=" << mode << " is not available yet\n";
    return 1;

}

bool TrainHost::loadEnvParams() {
    std::string error;
    if(!train.env_params_in.empty() && !env_params.loadFile(train.env_params_in, error)) {
        dy::log(dy::error) << "EnvParams: " << error << "\n";
        return false;
    }
    for(auto& [name, value] : train.set) {
        if(!env_params.setOverride(name, value, error)) {
            dy::log(dy::error) << "EnvParams: " << error << "\n";
            return false;
        }
        dy::log(dy::info) << "EnvParams: -set:" << name << "=" << value << "\n";
    }
    writeEnvParams(0);
    return true;
}

void TrainHost::writeEnvParams(int64_t steps) {
    if(train.env_params_out.empty()) return;
    auto values = env_params.resolved();
    if(envparams_written && values == written) return;
    EnvParamsFile file;
    file.behavior = "HeidelBotBehavior";
    file.written = steps;
    file.header["sha"] = DYNAMICAL_GIT_SHA;
    file.values = values;
    if(writeEnvParamsFile(train.env_params_out, file)) {
        written = values;
        envparams_written = true;
    } else {
        dy::log(dy::warning) << "EnvParams: could not write " << train.env_params_out << ", will retry on the next change\n";
    }
}

int TrainHost::bench() {

    const int64_t ticks = args.ticks > 0 ? args.ticks : 600;
    const size_t worlds = runner.worldCount();
    const unsigned threads = args.threads > 0 ? (unsigned) args.threads
        : (unsigned) std::max<size_t>(1, std::min<size_t>(worlds, std::thread::hardware_concurrency()));

    tf::Executor executor(threads);
    auto start = std::chrono::steady_clock::now();
    runner.step(ticks, &executor);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    double rate = seconds > 0. ? ticks / seconds : 0.;
    dy::log(dy::info) << "bench " << runner.scene().name << ": " << worlds << " worlds, " << threads << " threads, " << ticks
        << " ticks in " << seconds << " s: " << rate << " ticks/s, " << rate * worlds << " world-ticks/s\n";
    return 0;

}

int TrainHost::null() {

    if(train.port <= 0) {
        dy::log(dy::error) << "mode=null needs --mlagents-port <port>\n";
        return 1;
    }

    NullEnvironment::Config config;
    config.agents = sceneInt(runner.scene(), "agents", config.agents);
    config.episode_decisions = sceneInt(runner.scene(), "episode_decisions", config.episode_decisions);
    NullEnvironment env(config);

    mlagents::MlAgentsClient client(train.port, stopRequested);

    co::UnityRLInitializationOutputProto init_out;
    init_out.set_name("Dynamical");
    init_out.set_communication_version("1.5.0");
    init_out.set_package_version("4.0.0");
    auto* caps = init_out.mutable_capabilities();
    caps->set_baserlcapabilities(true);
    caps->set_concatenatedpngobservations(true);
    caps->set_compressedchannelmapping(true);
    caps->set_hybridactions(true);
    caps->set_traininganalytics(true);
    caps->set_variablelengthobservation(true);
    caps->set_multiagentgroups(true);

    auto stopped = [&]() {
        dy::log(dy::info) << stopSignal() << " received: exiting\n";
        return 0;
    };

    co::UnityRLInitializationInputProto init_in;
    co::UnityInputProto input;
    dy::log(dy::info) << "Connecting to the trainer on port " << train.port << "\n";
    auto status = client.initialize(init_out, init_in, input);
    if(status == mlagents::MlAgentsClient::Status::Stopped) return stopped();
    if(status == mlagents::MlAgentsClient::Status::Closed) {
        dy::log(dy::info) << "The trainer closed the connection during initialisation\n";
        return 0;
    }
    if(status != mlagents::MlAgentsClient::Status::Ok) {
        dy::log(dy::error) << "Trainer connection failed: " << client.error << "\n";
        return 2;
    }

    base_seed = init_in.seed();
    dy::log(dy::info) << "Trainer connected: communication " << init_in.communication_version() << ", package " << init_in.package_version()
        << ", seed " << init_in.seed() << ", num_areas " << init_in.num_areas() << " (ignored: " << config.agents << " agents of "
        << config.behavior << ", " << config.episode_decisions << " decisions per episode)\n";

    bool engine_logged = false;
    int64_t steps = 0;

    while(true) {

        if(stopRequested()) return stopped();

        const auto& rl = input.rl_input();

        std::vector<mlagents::ChannelMessage> messages;
        if(!mlagents::readChannelMessages(rl.side_channel(), messages)) dy::log(dy::warning) << "Side channel: truncated payload ignored\n";
        for(auto& m : messages) {
            if(m.channel == mlagents::envParamsChannel()) {
                mlagents::EnvParamMessage p;
                if(!mlagents::readEnvParam(m.payload, p)) {
                    dy::log(dy::warning) << "Side channel: malformed environment parameter\n";
                } else if(p.type == mlagents::EnvParamMessage::Type::Float) {
                    env_params.setLive(p.key, p.value);
                } else {
                    dy::log(dy::info) << "EnvParams: sampler " << p.key << " (seed " << p.seed << ") received\n";
                }
            } else if(m.channel == mlagents::engineConfigChannel()) {
                if(!engine_logged) dy::log(dy::info) << "Engine configuration received and ignored (headless, as fast as possible)\n";
                engine_logged = true;
            }
        }
        writeEnvParams(steps);

        if(rl.command() == co::QUIT) {
            dy::log(dy::info) << "QUIT received: exiting\n";
            return 0;
        }
        if(rl.command() == co::RESET) {
            env.reset();
        } else {
            std::string error;
            if(!env.step(rl, error)) {
                dy::log(dy::error) << "STEP: " << error << "\n";
                return 2;
            }
            steps++;
        }

        co::UnityOutputProto output;
        env.write(output);
        status = client.exchange(output, input);
        if(status == mlagents::MlAgentsClient::Status::Stopped) return stopped();
        if(status == mlagents::MlAgentsClient::Status::Closed) {
            dy::log(dy::info) << "The trainer closed the connection: exiting\n";
            return 0;
        }
        if(status != mlagents::MlAgentsClient::Status::Ok) {
            dy::log(dy::error) << "Trainer connection lost: " << client.error << "\n";
            return 2;
        }

    }

}
