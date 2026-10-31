#ifndef DY_NULL_ENV_H
#define DY_NULL_ENV_H

#include <cstdint>
#include <string>
#include <vector>

#include "mlagents/client.h"

namespace dy {

// mode=null (M0, mlagents-bridge.md): the trainer contract without worlds or physics. N agents of one behaviour,
// a fixed zero observation (the noToes layout: 138 x 2 stacked, Unity's sensor name), zero rewards, episodes
// ended by truncation after `episode_decisions` decisions. It measures the trainer's own ceiling.
class NullEnvironment {
public:
    struct Config {
        std::string behavior = "HeidelBotBehavior";
        int agents = 25;
        int episode_decisions = 1000;
        int observation_size = 138;
        int stack = 2;
        int actions = 120;
    };

    explicit NullEnvironment(const Config& config);

    // RESET: every agent starts a new episode and keeps its id
    void reset();

    // STEP: the actions of the agents that requested a decision in the last output, in that order
    // (RpcCommunicator.cs:361-368, :419-428). Done agents get none and start their next episode now
    bool step(const mlagents::co::UnityRLInputProto& input, std::string& error);

    // This window's AgentInfos (and the brain parameters with the first ones)
    void write(mlagents::co::UnityOutputProto& output);

    // Agents asking for a decision in the last output, in output order
    std::vector<int32_t> requesting() const;

    std::string sensorName() const;
    const Config config;

private:
    struct Agent {
        int32_t id;
        int decisions = 0;
        bool done = false;
    };
    std::vector<Agent> agents;
    bool brain_sent = false;
};

}

#endif
