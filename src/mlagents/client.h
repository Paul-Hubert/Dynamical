#ifndef DY_MLAGENTS_CLIENT_H
#define DY_MLAGENTS_CLIENT_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "mlagents_envs/communicator_objects/unity_message.pb.h"
#include "mlagents_envs/communicator_objects/unity_to_external.grpc.pb.h"

namespace dy::mlagents {

namespace co = communicator_objects;

// The engine side of the ML-Agents protocol 1.5.0: Python is the gRPC server on localhost:<port>, the engine
// calls UnityToExternalProto.Exchange repeatedly (mlagents-bridge.md, roles and protocol).
// Every call is issued once and awaited in 1 s slices, so a stop request is seen within a second without
// re-sending a message the trainer may already hold.
class MlAgentsClient {
public:
    enum class Status {
        Ok,
        Closed,         // the trainer answered with a header other than 200 (400 on close)
        Stopped,        // stop requested (CTRL_C) while waiting
        Failed,         // gRPC error or timeout: see error
    };

    MlAgentsClient(int port, std::function<bool()> stop_requested);

    // Unity's initialisation (RpcCommunicator.cs:235-236): Exchange(init output) answers the initialisation
    // input; then Exchange(empty message) answers the first command once the trainer resets. The trainer must
    // answer the first call within connect_timeout
    Status initialize(const co::UnityRLInitializationOutputProto& output, co::UnityRLInitializationInputProto& init,
        co::UnityInputProto& first, std::chrono::seconds connect_timeout = std::chrono::seconds(60));

    Status exchange(const co::UnityOutputProto& output, co::UnityInputProto& input);

    std::string error;

private:
    Status call(const co::UnityMessageProto& request, co::UnityMessageProto& reply, std::chrono::seconds timeout);

    std::function<bool()> stop_requested;
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<co::UnityToExternalProto::Stub> stub;
};

}

#endif
