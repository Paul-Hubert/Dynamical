#include "mock_trainer.h"

using namespace dy::test;

class MockTrainer::Service final : public co::UnityToExternalProto::Service {
public:
    explicit Service(MockTrainer& trainer) : trainer(trainer) {}

    grpc::Status Exchange(grpc::ServerContext* context, const co::UnityMessageProto* request, co::UnityMessageProto* response) override {
        std::unique_lock lock(trainer.mutex);
        trainer.requests.push_back(*request);
        trainer.changed.notify_all();
        // Python's servicer blocks until the trainer answers (child_conn.recv)
        trainer.changed.wait(lock, [&] { return trainer.answer.has_value() || trainer.shutting_down || context->IsCancelled(); });
        if(!trainer.answer) return grpc::Status::CANCELLED;
        *response = *trainer.answer;
        trainer.answer.reset();
        return grpc::Status::OK;
    }

private:
    MockTrainer& trainer;
};

MockTrainer::MockTrainer() : service(std::make_unique<Service>(*this)) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &bound_port);
    builder.RegisterService(service.get());
    server = builder.BuildAndStart();
}

MockTrainer::~MockTrainer() {
    {
        std::lock_guard lock(mutex);
        shutting_down = true;
    }
    changed.notify_all();
    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(2));
}

std::optional<co::UnityMessageProto> MockTrainer::receive(std::chrono::seconds timeout) {
    std::unique_lock lock(mutex);
    if(!changed.wait_for(lock, timeout, [&] { return !requests.empty(); })) return std::nullopt;
    auto message = requests.front();
    requests.pop_front();
    return message;
}

void MockTrainer::reply(const co::UnityMessageProto& message) {
    {
        std::lock_guard lock(mutex);
        answer = message;
    }
    changed.notify_all();
}

std::optional<co::UnityOutputProto> MockTrainer::initialize(const co::UnityInputProto& init, std::chrono::seconds timeout) {
    auto first = receive(timeout);
    if(!first) return std::nullopt;
    co::UnityMessageProto message;
    message.mutable_header()->set_status(200);
    *message.mutable_unity_input() = init;
    reply(message);
    // The engine's second message: received now, answered by the next exchange()
    if(!receive(timeout)) return std::nullopt;
    return first->unity_output();
}

std::optional<co::UnityOutputProto> MockTrainer::exchange(const co::UnityInputProto& input, std::chrono::seconds timeout) {
    send(input);
    auto output = receive(timeout);
    if(!output || output->header().status() != 200) return std::nullopt;
    return output->unity_output();
}

void MockTrainer::send(const co::UnityInputProto& input) {
    co::UnityMessageProto message;
    message.mutable_header()->set_status(200);
    *message.mutable_unity_input() = input;
    reply(message);
}

void MockTrainer::close() {
    co::UnityMessageProto message;
    message.mutable_header()->set_status(400);
    reply(message);
}
