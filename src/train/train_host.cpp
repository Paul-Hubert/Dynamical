#include "train_host.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include <filesystem>
#include <fstream>

#include "env_params_file.h"
#include "logic/agents/agent_env_params.h"
#include "logic/agents/agent_io.h"
#include "logic/agents/ground_field.h"
#include "logic/agents/humanbot.h"
#ifdef DYNAMICAL_ONNX
#include "onnx/policy_runner.h"
#endif
#include "mlagents/client.h"
#include "mlagents/side_channels.h"
#include "mlagents_envs/communicator_objects/agent_info_action_pair.pb.h"
#include "mlagents_envs/communicator_objects/brain_parameters.pb.h"
#include "mlagents_envs/communicator_objects/demonstration_meta.pb.h"
#include "null_env.h"
#include "worlds_env.h"
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

    std::string mode = args.mode.empty() ? "train" : args.mode;
    if(mode == "train" && WorldsEnvironment::check(runner).empty()) {
        // envparams.txt names one behaviour: the first by name when the scene has several
        if(auto names = WorldsEnvironment::behaviors(runner); !names.empty()) behavior = names[0];
    }

    if(!loadEnvParams()) return 1;

    if(mode == "bench") return bench();
    if(mode == "null") return null();
    if(mode == "train") return agents();
    if(mode == "record-demo") return recordDemo();
    if(mode == "eval") return animalWorlds() ? evalAnimals() : eval();

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
    file.behavior = behavior;
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

void TrainHost::pushEnvParams() {
    auto values = env_params.resolved();
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& reg = runner.world(i).reg;
        if(auto* existing = reg.ctx().find<AgentEnvParams>()) existing->values = values;
        else reg.ctx().emplace<AgentEnvParams>().values = values;
    }
}

int TrainHost::recordDemo() {

    if(runner.worldCount() != 1) {
        dy::log(dy::error) << "mode=record-demo needs exactly one world\n";
        return 1;
    }
    const auto& vars = runner.scene().resolved["variables"];
    int decisions = sceneInt(runner.scene(), "decisions", 3000);
    std::string out_path = vars.contains("out") ? vars["out"]["default"].get<std::string>() : "";
    if(out_path.empty()) {
        dy::log(dy::error) << "mode=record-demo needs var.out=<file.demo>\n";
        return 1;
    }

    auto& world = runner.world(0);
    auto& io = world.reg.ctx().get<AgentIO>();
    int period = (int) world.reg.ctx().get<WorldClock>().decision_period;
    if(std::string problem = animalDemoProblem(); !problem.empty()) {
        dy::log(dy::error) << "mode=record-demo: " << problem << "\n";
        return 1;
    }

    mlagents::co::DemonstrationMetaProto meta;
    meta.set_api_version(1);
    meta.set_demonstration_name(std::filesystem::path(out_path).stem().string().substr(0, 16));

    std::string records;
    int steps = 0, episodes = 0;
    {
        google::protobuf::io::StringOutputStream stream(&records);
        google::protobuf::io::CodedOutputStream coded(&stream);
        // Warm up one window: the anchor velocities are valid from the second observation of the track
        runner.step(period);
        bool finished = false;
        while(!finished) {
            runner.step(period);
            if(stopRequested()) return 1;
            // A gait demo's warm-up between two routes belongs to no episode
            if(!io.recordable) continue;
            recordAnimalReplay();
            mlagents::co::AgentInfoActionPairProto pair;
            auto* info = pair.mutable_agent_info();
            info->set_reward(0.f);
            info->set_done(io.done);
            info->set_max_step_reached(io.done);
            info->set_id(0);
            auto* obs = info->add_observations();
            obs->add_shape(io.observation_size);
            obs->set_compression_type(mlagents::co::NONE);
            obs->mutable_float_data()->mutable_data()->Add(io.observation.begin(), io.observation.end());
            obs->add_dimension_properties(0);
            obs->set_observation_type(mlagents::co::DEFAULT);
            obs->set_name(io.sensor_name);
            // The agent's other sensors (a walker's order): a demonstration has the layout of the behaviour
            for(const AgentSensor& sensor : io.sensors) {
                auto* more = info->add_observations();
                more->set_compression_type(mlagents::co::NONE);
                more->mutable_float_data()->mutable_data()->Add(sensor.data.begin(), sensor.data.end());
                if(sensor.rows > 0) {
                    more->add_shape(sensor.rows);
                    more->add_shape(sensor.size);
                    more->add_dimension_properties(4);
                    more->add_dimension_properties(1);
                } else {
                    more->add_shape(sensor.size);
                    more->add_dimension_properties(0);
                }
                more->set_observation_type(mlagents::co::DEFAULT);
                more->set_name(sensor.name);
            }
            auto* act = pair.mutable_action_info();
            act->mutable_continuous_actions()->Resize(io.action_size, 0.f);
            coded.WriteVarint32((uint32_t) pair.ByteSizeLong());
            pair.SerializeToCodedStream(&coded);
            steps++;
            if(io.done) {
                episodes++;
                if(steps >= decisions) finished = true;
            }
        }
    }

    meta.set_number_steps(steps);
    meta.set_number_episodes(episodes);
    meta.set_mean_reward(0.f);

    mlagents::co::BrainParametersProto brain;
    brain.set_brain_name(io.behavior + "?team=0");
    brain.set_is_training(false);
    brain.mutable_action_spec()->set_num_continuous_actions(io.action_size);
    brain.mutable_action_spec()->set_num_discrete_actions(0);

    std::ofstream out(out_path, std::ios::binary);
    if(!out) {
        dy::log(dy::error) << "Cannot write " << out_path << "\n";
        return 1;
    }
    {
        // The metadata in its 32-byte header, the brain from byte 33, then the records: demo_loader.py's layout
        std::string header;
        {
            google::protobuf::io::StringOutputStream s(&header);
            google::protobuf::io::CodedOutputStream c(&s);
            c.WriteVarint32((uint32_t) meta.ByteSizeLong());
            meta.SerializeToCodedStream(&c);
        }
        if(header.size() > 33) {
            dy::log(dy::error) << "Demo metadata too long\n";
            return 1;
        }
        header.resize(33, '\0');
        out.write(header.data(), header.size());
        std::string body;
        {
            google::protobuf::io::StringOutputStream s(&body);
            google::protobuf::io::CodedOutputStream c(&s);
            c.WriteVarint32((uint32_t) brain.ByteSizeLong());
            brain.SerializeToCodedStream(&c);
        }
        out.write(body.data(), body.size());
        out.write(records.data(), records.size());
    }
    dy::log(dy::info) << "Demo " << out_path << ": " << steps << " decisions, " << episodes << " episodes\n";
    writeAnimalChecks();
    return 0;

}

int TrainHost::eval() {

#ifndef DYNAMICAL_ONNX
    dy::log(dy::error) << "mode=eval needs a build with ONNX Runtime\n";
    return 1;
#else
    const auto& vars = runner.scene().resolved["variables"];
    std::string model = vars.contains("model") ? vars["model"]["default"].get<std::string>() : "";
    std::string out_path = vars.contains("out") ? vars["out"]["default"].get<std::string>() : "";
    int episodes = sceneInt(runner.scene(), "episodes", 100);
    if(model.empty()) {
        dy::log(dy::error) << "mode=eval needs var.model=<file.onnx>\n";
        return 1;
    }
    auto policy = std::make_shared<PolicyRunner>(model, humanbot_observations, humanbot_actions);
    // var.stochastic=1: sample the actions as in training (mean + noise) instead of the mean
    bool stochastic = vars.contains("stochastic") && vars["stochastic"]["default"].get<int>() != 0;
    // var.cot_out=<file>: per settled decision, 5 floats: positive W, negative W, torque variation N m/s, smoothed speed,
    // target speed (the energy reward's inputs, so any multipliers, torque-rate weight and energyScale can be replayed)
    std::string cot_path = vars.contains("cot_out") ? vars["cot_out"]["default"].get<std::string>() : "";
    std::vector<float> cot_data;
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& reg = runner.world(i).reg;
        reg.ctx().insert_or_assign(PolicyHandle { policy, !stochastic });
        reg.ctx().get<AgentIO>().reset_requested = true;
        runner.world(i).systems.fixed_observe_only();
    }
    int period = (int) runner.world(0).reg.ctx().get<WorldClock>().decision_period;
    tf::Executor executor(std::max(1u, std::thread::hardware_concurrency() / 2));

    struct Sum { double ticks = 0, dead = 0, distance = 0, seconds = 0, head = 0; int n = 0; } sum;
    // var.replay=<prefix>: the first episode of world 0 as a pose track (playable in rig_anim, var.track=...)
    std::string replay = vars.contains("replay") ? vars["replay"]["default"].get<std::string>() : "";
    std::vector<float> replay_data;
    bool replay_open = !replay.empty();
    // var.legs=<file.csv>: one row per stretch between two target draws, with the ground it crossed
    std::string legs_path = vars.contains("legs") ? vars["legs"]["default"].get<std::string>() : "";
    std::ofstream legs;
    if(!legs_path.empty()) {
        legs.open(legs_path);
        legs << "episode,world,leg,seconds,distance,progress,rise,pitch_deg,up_deg,down_deg,across_deg,mean_across_deg,strikes,fell,route_lost\n";
    }
    double strikes = 0.;
    while(sum.n < episodes) {
        runner.step(period, &executor);
        if(replay_open) {
            auto& bot = runner.world(0).reg.ctx().get<HumanBot>();
            for(size_t i = 0; i < bot.body->segments.size(); i++) {
                glm::vec3 p = bot.state.position[i];
                glm::quat q = bot.state.rotation[i];
                replay_data.insert(replay_data.end(), { p.x, p.y, p.z, q.x, q.y, q.z, q.w });
            }
            if(runner.world(0).reg.ctx().get<AgentIO>().done || replay_data.size() > 16 * 7 * 4000) {
                replay_open = false;
                auto& rbot = runner.world(0).reg.ctx().get<HumanBot>();
                int frames = (int) (replay_data.size() / (16 * 7));
                std::ofstream(replay + ".track.bin", std::ios::binary).write(reinterpret_cast<const char*>(replay_data.data()), replay_data.size() * sizeof(float));
                nlohmann::json header = { { "format_version", 1 }, { "body", rbot.body->name }, { "controller", "replay" }, { "dt", (float) (runner.world(0).reg.ctx().get<WorldClock>().dt * period) },
                    { "frames", frames }, { "segments", (int) rbot.body->segments.size() }, { "frame", "engine" }, { "state_speed", 1.f } };
                std::ofstream(replay + ".track.json") << header.dump(1);
            }
        }
        for(size_t i = 0; !cot_path.empty() && i < runner.worldCount(); i++) {
            auto& reg = runner.world(i).reg;
            const auto& io = reg.ctx().get<AgentIO>();
            const auto& bot = reg.ctx().get<HumanBot>();
            if(io.done || bot.tick * reg.ctx().get<WorldClock>().dt < bot.params.stats_settle_seconds) continue;
            float row[4] = { NAN, NAN, NAN, NAN };
            const char* keys[4] = { "EnergyProvider/Positive Energy", "EnergyProvider/Negative Energy",
                                    "EnergyProvider/Torque Variation Rate", "EnergyProvider/Smoothed Speed" };
            for(const auto& s : io.stats)
                for(int k = 0; k < 4; k++)
                    if(s.key == keys[k]) row[k] = s.value;
            if(std::isnan(row[0]) || std::isnan(row[2])) continue;
            cot_data.insert(cot_data.end(), { row[0], row[1], row[2], row[3], bot.target_speed });
        }
        for(size_t i = 0; i < runner.worldCount(); i++) {
            auto& reg = runner.world(i).reg;
            auto& io = reg.ctx().get<AgentIO>();
            auto& bot = reg.ctx().get<HumanBot>();
            if(!io.done) continue;
            sum.ticks += bot.tick;
            sum.dead += bot.killed ? 1 : 0;
            double dt = reg.ctx().get<WorldClock>().dt;
            sum.seconds += bot.tick * dt;
            // Along the path: the target moves during an episode (on a terrain, to another direction)
            size_t last = 0;
            for(size_t l = 0; l < bot.legs.size(); l++) if(bot.legs[l].ticks > 0) last = l;
            for(size_t l = 0; l < bot.legs.size(); l++) {
                const WalkLeg& leg = bot.legs[l];
                // The target moves on the tick the episode ends on: that stretch was never walked
                if(leg.ticks <= 0) continue;
                glm::vec3 moved = horizontal(leg.to - leg.from);
                float distance = glm::length(moved), progress = glm::dot(moved, leg.direction);
                sum.distance += distance;
                strikes += leg.strikes;
                if(!legs.is_open()) continue;
                RouteSlope slope;
                if(bot.ground) {
                    glm::vec3 from(leg.from.x, bot.groundUnder(leg.from), leg.from.z);
                    slope = measureRoute(*bot.ground, from, distance > 0.5f ? moved : leg.direction, std::max(distance, 0.5f), 0.3f, 0.25f);
                }
                const float deg = 57.2957795f;
                legs << sum.n << "," << i << "," << l << "," << leg.ticks * dt << "," << distance << "," << progress << "," << slope.rise << ","
                    << std::atan2(slope.rise, std::max(distance, 0.5f)) * deg << "," << slope.up * deg << "," << slope.down * deg << "," << slope.across * deg << ","
                    << slope.mean_across * deg << "," << leg.strikes << "," << (leg.fell ? 1 : 0) << "," << (bot.route_lost && !bot.killed && l == last ? 1 : 0) << "\n";
            }
            sum.head += bot.currentHeight();
            sum.n++;
        }
    }
    nlohmann::json summary = {
        { "model", model }, { "episodes", sum.n }, { "dead_rate", sum.dead / sum.n },
        { "mean_length_ticks", sum.ticks / sum.n }, { "mean_speed", sum.distance / std::max(sum.seconds, 1e-9) }, { "mean_final_height", sum.head / sum.n },
        { "steps_per_second", strikes / std::max(sum.seconds, 1e-9) }, { "step_length", sum.distance / std::max(strikes, 1.) } };
    // The energy books over every episode: net residual (S7.5) and tick-wise |residual|, both against sum |work|
    double abs_work = 0., abs_residual = 0., net_residual = 0.;
    double devita_pos = 0., devita_neg = 0., devita_stances = 0.;
    FormulaTotals formulas;
    for(size_t i = 0; i < runner.worldCount(); i++) {
        const auto& books = runner.world(i).reg.ctx().get<HumanBot>().books;
        abs_work += books.cum_abs_work;
        abs_residual += books.cum_residual;
        net_residual += books.cum_net_residual;
        const auto& devita = runner.world(i).reg.ctx().get<HumanBot>().devita;
        devita_pos += devita.total_pos;
        devita_neg += devita.total_neg;
        devita_stances += (double) devita.total_stances;
        formulas += runner.world(i).reg.ctx().get<HumanBot>().formulas.totals;
    }
    if(devita_pos > 0.) {
        summary["devita_neg_pos"] = devita_neg / devita_pos;
        summary["devita_pos_per_stance"] = devita_pos / devita_stances;
        summary["devita_neg_per_stance"] = devita_neg / devita_stances;
        summary["devita_stances"] = devita_stances;
    }
    summary["closure_net"] = net_residual / std::max(abs_work, 1e-9);
    summary["closure_tickwise"] = abs_residual / std::max(abs_work, 1e-9);
    auto split = [](const WorkSum& w) { return nlohmann::json { { "pos", w.pos }, { "neg", w.neg } }; };
    auto ratio = [](double a, double b) { return b > 0. ? a / b : 0.; };
    summary["energy_formulas"] = {
        { "ticks", formulas.ticks }, { "seconds", formulas.seconds }, { "metres", formulas.metres }, { "stances", formulas.stances },
        { "torque_variation_Nm", formulas.torque_variation },
        { "full_body", { { "internal_abs_delta_L", formulas.lagrangian_abs }, { "internal_L", split(formulas.lagrangian) }, { "body_energy", split(formulas.body_energy) },
                         { "rne", split(formulas.rne) }, { "drive", split(formulas.drive) } } },
        { "devita_stance", { { "rne_sagittal", split(formulas.rne_sagittal) }, { "drive_sagittal", split(formulas.drive_sagittal) },
                             { "rne_3d", split(formulas.rne_stance_3d) }, { "drive_3d", split(formulas.drive_stance_3d) } } },
        { "verification", {
            { "rne_force_vs_jolt_point_lambda", ratio(formulas.point_error, formulas.point_reference) },
            { "rne_torque_vs_motor_no_limit", ratio(formulas.torque_error_free, formulas.torque_reference_free) },
            { "joint_ticks_no_limit", formulas.joint_ticks_free }, { "joint_ticks_limit", formulas.joint_ticks_limit },
            { "limit_torque_share", ratio(formulas.limit_torque, formulas.limit_torque + formulas.torque_reference_free) },
            { "jolt_contact_vs_momentum_balance", ratio(formulas.contact_error, formulas.contact_reference) },
            { "root_residual_force_vs_weight", ratio(formulas.root_linear, formulas.weight_impulse) },
            { "root_residual_torque_nms_per_tick", ratio(formulas.root_angular, (double) formulas.ticks) },
            { "closure_net", ratio(formulas.closure_net, formulas.closure_gross) },
            { "closure_tickwise", ratio(formulas.closure_tickwise, formulas.closure_gross) },
            { "closure_with_point_work_net", ratio(formulas.closure_point_net, formulas.closure_gross) },
            { "closure_with_point_work_tickwise", ratio(formulas.closure_point_tickwise, formulas.closure_gross) },
            { "contact_work_J", formulas.contact_work }, { "clamped_ticks", formulas.clamped_ticks } } } };
    if(!cot_path.empty()) {
        std::ofstream(cot_path, std::ios::binary).write(reinterpret_cast<const char*>(cot_data.data()), cot_data.size() * sizeof(float));
        summary["cot_decisions"] = cot_data.size() / 5;
    }
    summary["stochastic"] = stochastic;
    dy::log(dy::info) << "eval " << summary.dump() << "\n";
    if(!out_path.empty()) std::ofstream(out_path) << summary.dump(2) << "\n";
    return 0;
#endif

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

    return serve([&](int64_t) {
        dy::log(dy::info) << "Null environment: " << config.agents << " agents of " << config.behavior << ", "
            << config.episode_decisions << " decisions per episode\n";
        return std::make_unique<NullEnvironment>(config);
    });

}

int TrainHost::agents() {

    if(train.port <= 0) {
        dy::log(dy::error) << "mode=train needs --mlagents-port <port>\n";
        return 1;
    }
    if(auto error = WorldsEnvironment::check(runner); !error.empty()) {
        dy::log(dy::error) << "Scene " << runner.scene().name << " cannot train: " << error << "\n";
        return 1;
    }
    if(auto names = WorldsEnvironment::behaviors(runner); !names.empty()) behavior = names[0];
    const size_t worlds = runner.worldCount();
    const unsigned threads = args.threads > 0 ? (unsigned) args.threads : 1u;

    return serve([&, worlds, threads](int64_t seed) {
        // The worlds again, seeded from the trainer's seed (it includes the worker id)
        runner.restart((uint64_t) seed);
        pushEnvParams();
        auto env = std::make_unique<WorldsEnvironment>(runner, threads);
        dy::log(dy::info) << "Scene " << runner.scene().name << ": " << worlds << " worlds, " << env->describe() << ", a window every "
            << env->decisionPeriod() << " ticks, " << threads << " threads\n";
        return env;
    });

}

int TrainHost::serve(const std::function<std::unique_ptr<TrainerEnvironment>(int64_t seed)>& connected) {

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
        << ", seed " << init_in.seed() << ", num_areas " << init_in.num_areas() << " (ignored)\n";
    std::unique_ptr<TrainerEnvironment> env = connected(init_in.seed());

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
        if(!messages.empty()) pushEnvParams();

        if(rl.command() == co::QUIT) {
            dy::log(dy::info) << "QUIT received: exiting\n";
            return 0;
        }
        if(rl.command() == co::RESET) {
            env->reset();
        } else {
            std::string error;
            if(!env->step(rl, error)) {
                dy::log(dy::error) << "STEP: " << error << "\n";
                return 2;
            }
            steps++;
        }

        co::UnityOutputProto output;
        env->write(output);
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
