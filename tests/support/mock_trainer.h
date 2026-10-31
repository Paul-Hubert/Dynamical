#ifndef DY_MOCK_TRAINER_H
#define DY_MOCK_TRAINER_H

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

#include <grpcpp/grpcpp.h>

#include "mlagents_envs/communicator_objects/unity_to_external.grpc.pb.h"

namespace dy::test {

namespace co = communicator_objects;

// An in-process gRPC server that behaves as the fork's Python RpcCommunicator (rpc_communicator.py): every
// Exchange from the engine is handed over to the test, and answered when the test sends the next input
class MockTrainer {
public:
    MockTrainer();
    ~MockTrainer();

    int port() const { return bound_port; }

    // rpc_communicator.py initialize(): the engine's first message (initialisation output) is answered with
    // `init`; the engine's second (empty) message stays pending until the next exchange()
    std::optional<co::UnityOutputProto> initialize(const co::UnityInputProto& init, std::chrono::seconds timeout = std::chrono::seconds(60));

    // exchange(): answers the pending message with `input`, returns the engine's next output (nullopt on timeout)
    std::optional<co::UnityOutputProto> exchange(const co::UnityInputProto& input, std::chrono::seconds timeout = std::chrono::seconds(30));

    // Answers the pending message without waiting for another one (QUIT)
    void send(const co::UnityInputProto& input);

    // close(): answers the pending message with header 400
    void close();

private:
    class Service;

    std::optional<co::UnityMessageProto> receive(std::chrono::seconds timeout);
    void reply(const co::UnityMessageProto& message);

    std::unique_ptr<Service> service;
    std::unique_ptr<grpc::Server> server;
    int bound_port = 0;

    std::mutex mutex;
    std::condition_variable changed;
    std::deque<co::UnityMessageProto> requests;
    std::optional<co::UnityMessageProto> answer;
    bool shutting_down = false;
};

}

#endif
