#include "worlds_env.h"

#include <taskflow/taskflow.hpp>

#include <map>

#include "mlagents/side_channels.h"
#include "scene/world.h"

using namespace dy;
namespace co = mlagents::co;

std::string WorldsEnvironment::check(ScenarioRunner& runner) {
    if(runner.worldCount() == 0) return "the scene has no worlds";
    const AgentIO* first = nullptr;
    uint32_t first_period = 0;
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& reg = runner.world(i).reg;
        const auto* io = reg.ctx().find<AgentIO>();
        if(!io || io->behavior.empty()) return "world " + std::to_string(i) + " has no agent";
        uint32_t period = reg.ctx().get<WorldClock>().decision_period;
        if(!first) {
            first = io;
            first_period = period;
        } else if(io->behavior != first->behavior || io->observation_size != first->observation_size || io->action_size != first->action_size) {
            return "world " + std::to_string(i) + " has another behaviour or layout than world 0";
        } else if(period != first_period) {
            return "world " + std::to_string(i) + " has another decision period than world 0";
        }
    }
    return {};
}

WorldsEnvironment::WorldsEnvironment(ScenarioRunner& runner, unsigned threads) : runner(runner) {
    if(threads > 1) executor = std::make_unique<tf::Executor>(threads);
    for(size_t i = 0; i < runner.worldCount(); i++) agents.push_back(&runner.world(i).reg.ctx().get<AgentIO>());
    done.assign(agents.size(), 0);
    period = runner.world(0).reg.ctx().get<WorldClock>().decision_period;
}

WorldsEnvironment::~WorldsEnvironment() = default;

void WorldsEnvironment::reset() {
    // A fresh observation of every agent without stepping
    for(size_t i = 0; i < agents.size(); i++) {
        agents[i]->reset_requested = true;
        runner.world(i).systems.fixed_observe_only();
    }
    done.assign(agents.size(), 0);
}

bool WorldsEnvironment::step(const co::UnityRLInputProto& input, std::string& error) {

    size_t requested = 0;
    for(char d : done) requested += d ? 0 : 1;

    const co::UnityRLInputProto::ListAgentActionProto* list = nullptr;
    if(auto it = input.agent_actions().find(behavior()); it != input.agent_actions().end()) list = &it->second;
    size_t received = list ? (size_t) list->value_size() : 0;
    if(received != requested) {
        error = "expected actions for " + std::to_string(requested) + " agents, got " + std::to_string(received);
        return false;
    }

    int k = 0;
    for(size_t i = 0; i < agents.size(); i++) {
        auto& io = *agents[i];
        if(done[i]) {
            std::fill(io.action.begin(), io.action.end(), 0.f);
            continue;
        }
        const auto& action = list->value(k++);
        if(action.continuous_actions_size() != io.action_size) {
            error = "expected " + std::to_string(io.action_size) + " continuous actions, got " + std::to_string(action.continuous_actions_size());
            return false;
        }
        for(int a = 0; a < io.action_size; a++) io.action[a] = action.continuous_actions(a);
    }

    runner.step(period, executor.get());
    return true;

}

void WorldsEnvironment::write(co::UnityOutputProto& output) {

    auto& infos = (*output.mutable_rl_output()->mutable_agentinfos())[behavior()];
    struct Sum { double total = 0.; int count = 0; int aggregation = 0; };
    std::map<std::string, Sum> stats;

    for(size_t i = 0; i < agents.size(); i++) {
        const auto& io = *agents[i];
        auto* info = infos.add_value();
        info->set_id((int32_t) i);
        info->set_reward(io.reward);
        info->set_done(io.done);
        info->set_max_step_reached(io.done && io.truncated);
        info->set_group_id(0);
        info->set_group_reward(0.f);
        auto* obs = info->add_observations();
        obs->add_shape(io.observation_size);
        obs->set_compression_type(co::NONE);
        obs->mutable_float_data()->mutable_data()->Add(io.observation.begin(), io.observation.end());
        obs->add_dimension_properties(0);
        obs->set_observation_type(co::DEFAULT);
        obs->set_name(io.sensor_name);
        done[i] = io.done ? 1 : 0;
        for(auto& s : io.stats) {
            auto& sum = stats[s.key];
            sum.total += s.value;
            sum.count++;
            sum.aggregation = s.aggregation;
        }
    }

    // One averaged value per key and window, with its Unity aggregation (stats_side_channel.py averages them over
    // the summary period). Per-agent values would be one message per key, agent and decision
    if(!stats.empty()) {
        std::vector<mlagents::ChannelMessage> messages;
        for(auto& [key, sum] : stats) {
            messages.push_back({ mlagents::statsChannel(), mlagents::writeStat(key, (float) (sum.total / sum.count), (mlagents::StatAggregation) sum.aggregation) });
        }
        output.mutable_rl_output()->set_side_channel(mlagents::writeChannelMessages(messages));
    }

    if(!brain_sent) {
        auto* brain = output.mutable_rl_initialization_output()->add_brain_parameters();
        brain->set_brain_name(behavior());
        brain->set_is_training(true);
        brain->mutable_action_spec()->set_num_continuous_actions(agents[0]->action_size);
        brain->mutable_action_spec()->set_num_discrete_actions(0);
        brain_sent = true;
    }

}
