#include "client.h"

#include <grpcpp/grpcpp.h>

using namespace dy::mlagents;

MlAgentsClient::MlAgentsClient(int port, std::function<bool()> stop_requested) : stop_requested(std::move(stop_requested)) {
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(-1);
    args.SetMaxSendMessageSize(-1);
    channel = grpc::CreateCustomChannel("localhost:" + std::to_string(port), grpc::InsecureChannelCredentials(), args);
    stub = co::UnityToExternalProto::NewStub(channel);
}

MlAgentsClient::Status MlAgentsClient::call(const co::UnityMessageProto& request, co::UnityMessageProto& reply, std::chrono::seconds timeout) {

    grpc::ClientContext context;
    // The trainer starts its server before launching us: wait for it rather than fail on the first attempt
    context.set_wait_for_ready(true);
    auto start = std::chrono::system_clock::now();
    if(timeout.count() > 0) context.set_deadline(start + timeout);

    grpc::CompletionQueue queue;
    grpc::Status status;
    auto rpc = stub->AsyncExchange(&context, request, &queue);
    rpc->Finish(&reply, &status, (void*) 1);

    bool stopped = false;
    while(true) {
        void* tag;
        bool ok;
        auto next = queue.AsyncNext(&tag, &ok, std::chrono::system_clock::now() + std::chrono::seconds(1));
        if(next == grpc::CompletionQueue::GOT_EVENT) break;
        if(next == grpc::CompletionQueue::SHUTDOWN) {
            error = "completion queue shut down";
            return Status::Failed;
        }
        if(!stopped && stop_requested && stop_requested()) {
            // Cancel and wait for the call to finish before the context goes away
            context.TryCancel();
            stopped = true;
        }
    }
    queue.Shutdown();
    void* tag;
    bool ok;
    while(queue.Next(&tag, &ok)) {}

    if(stopped) return Status::Stopped;
    if(!status.ok()) {
        error = "gRPC " + std::to_string((int) status.error_code()) + ": " + status.error_message();
        return Status::Failed;
    }
    if(reply.header().status() != 200) return Status::Closed;
    return Status::Ok;

}

MlAgentsClient::Status MlAgentsClient::initialize(const co::UnityRLInitializationOutputProto& output, co::UnityRLInitializationInputProto& init,
    co::UnityInputProto& first, std::chrono::seconds connect_timeout) {

    co::UnityMessageProto request, reply;
    request.mutable_header()->set_status(200);
    *request.mutable_unity_output()->mutable_rl_initialization_output() = output;

    Status s = call(request, reply, connect_timeout);
    if(s == Status::Failed && error.rfind("gRPC 4:", 0) == 0) error = "the trainer did not answer within " + std::to_string(connect_timeout.count()) + " s (" + error + ")";
    if(s != Status::Ok) return s;
    init = reply.unity_input().rl_initialization_input();

    // The second message is empty: its answer is the first command (RESET)
    co::UnityMessageProto empty, command;
    empty.mutable_header()->set_status(200);
    s = call(empty, command, std::chrono::seconds(0));
    if(s != Status::Ok) return s;
    first = command.unity_input();
    return Status::Ok;

}

MlAgentsClient::Status MlAgentsClient::exchange(const co::UnityOutputProto& output, co::UnityInputProto& input) {

    co::UnityMessageProto request, reply;
    request.mutable_header()->set_status(200);
    *request.mutable_unity_output() = output;

    Status s = call(request, reply, std::chrono::seconds(0));
    if(s == Status::Ok) input = reply.unity_input();
    return s;

}
