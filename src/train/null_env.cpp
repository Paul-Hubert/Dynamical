#include "null_env.h"

using namespace dy;
namespace co = mlagents::co;

NullEnvironment::NullEnvironment(const Config& config) : config(config) {
    // Ids are assigned once per agent and kept across episodes, as the fork does (Agent.cs:489)
    for(int i = 0; i < config.agents; i++) agents.push_back({ i });
}

std::string NullEnvironment::sensorName() const {
    // StackingSensor.cs:62 around VectorSensor.cs:28
    return "StackingSensor_size" + std::to_string(config.stack) + "_VectorSensor_size" + std::to_string(config.observation_size);
}

void NullEnvironment::reset() {
    for(auto& a : agents) {
        a.decisions = 0;
        a.done = false;
    }
}

std::vector<int32_t> NullEnvironment::requesting() const {
    std::vector<int32_t> ids;
    for(auto& a : agents) {
        if(!a.done) ids.push_back(a.id);
    }
    return ids;
}

bool NullEnvironment::step(const co::UnityRLInputProto& input, std::string& error) {

    const auto requested = requesting();
    size_t received = 0;
    if(auto it = input.agent_actions().find(config.behavior); it != input.agent_actions().end()) {
        received = it->second.value_size();
        for(auto& action : it->second.value()) {
            if(action.continuous_actions_size() != config.actions) {
                error = "expected " + std::to_string(config.actions) + " continuous actions, got " + std::to_string(action.continuous_actions_size());
                return false;
            }
        }
    }
    if(received != requested.size()) {
        error = "expected actions for " + std::to_string(requested.size()) + " agents, got " + std::to_string(received);
        return false;
    }

    for(auto& a : agents) {
        if(a.done) {
            // Its terminal step went out last window: the new episode's first observation goes out now
            a.done = false;
            a.decisions = 0;
        } else {
            a.decisions++;
            a.done = a.decisions >= config.episode_decisions;
        }
    }
    return true;

}

void NullEnvironment::write(co::UnityOutputProto& output) {

    auto& infos = (*output.mutable_rl_output()->mutable_agentinfos())[config.behavior];
    const int size = config.observation_size * config.stack;
    const std::string name = sensorName();

    for(auto& a : agents) {
        auto* info = infos.add_value();
        info->set_id(a.id);
        info->set_reward(0.f);
        info->set_done(a.done);
        info->set_max_step_reached(a.done);
        info->set_group_id(0);
        info->set_group_reward(0.f);
        auto* obs = info->add_observations();
        obs->add_shape(size);
        obs->set_compression_type(co::NONE);
        obs->mutable_float_data()->mutable_data()->Resize(size, 0.f);
        obs->add_dimension_properties(0);
        obs->set_observation_type(co::DEFAULT);
        obs->set_name(name);
    }

    // With the first AgentInfos of the behaviour (RpcCommunicator.cs GetTempUnityRlInitializationOutput)
    if(!brain_sent) {
        auto* brain = output.mutable_rl_initialization_output()->add_brain_parameters();
        brain->set_brain_name(config.behavior);
        brain->set_is_training(true);
        brain->mutable_action_spec()->set_num_continuous_actions(config.actions);
        brain->mutable_action_spec()->set_num_discrete_actions(0);
        brain_sent = true;
    }

}
