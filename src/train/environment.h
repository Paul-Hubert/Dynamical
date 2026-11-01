#ifndef DY_TRAINER_ENVIRONMENT_H
#define DY_TRAINER_ENVIRONMENT_H

#include <string>

#include "mlagents/client.h"

namespace dy {

// What TrainHost serves to the trainer after the handshake: RESET, STEP, and the output of each
class TrainerEnvironment {
public:
    virtual ~TrainerEnvironment() = default;

    // RESET: every agent starts a new episode and keeps its id
    virtual void reset() = 0;

    // STEP: the actions of the agents that requested a decision in the last output, in that order
    // (RpcCommunicator.cs:361-368, :419-428). false with `error` on a malformed input
    virtual bool step(const mlagents::co::UnityRLInputProto& input, std::string& error) = 0;

    // This window's AgentInfos (and the brain parameters with the first ones)
    virtual void write(mlagents::co::UnityOutputProto& output) = 0;
};

}

#endif
