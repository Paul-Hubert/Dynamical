#include "policy_runner.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>

using namespace dy;

namespace {

// One environment per process
Ort::Env& env() {
    static Ort::Env instance(ORT_LOGGING_LEVEL_WARNING, "dynamical");
    return instance;
}

int64_t scalarOutput(Ort::Value& value) {
    auto type = value.GetTensorTypeAndShapeInfo().GetElementType();
    if(type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) return (int64_t) value.GetTensorData<float>()[0];
    if(type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) return value.GetTensorData<int64_t>()[0];
    if(type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) return value.GetTensorData<int32_t>()[0];
    throw PolicyError("unexpected element type of a constant output");
}

}

struct PolicyRunner::Impl {
    Ort::Session session { nullptr };
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
};

PolicyRunner::PolicyRunner(const std::filesystem::path& onnx, int observation_size, int action_size, int threads) : impl(std::make_unique<Impl>()) {

    model.path = onnx;
    if(!std::filesystem::exists(onnx)) throw PolicyError("no file " + onnx.string());

    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(threads);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        impl->session = Ort::Session(env(), onnx.c_str(), options);

        auto& session = impl->session;
        Ort::AllocatorWithDefaultOptions allocator;

        if(session.GetInputCount() != 1) {
            throw PolicyError(std::to_string(session.GetInputCount()) + " inputs, expected obs_0 only (one vector sensor)");
        }
        std::string input = session.GetInputNameAllocated(0, allocator).get();
        auto shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if(input != "obs_0" || shape.size() != 2) throw PolicyError("input " + input + " is not obs_0 [batch, n]");
        if(shape[1] != observation_size) {
            throw PolicyError("the model observes " + std::to_string(shape[1]) + " floats, the layout has " + std::to_string(observation_size));
        }

        std::vector<std::string> outputs;
        for(size_t i = 0; i < session.GetOutputCount(); i++) outputs.push_back(session.GetOutputNameAllocated(i, allocator).get());
        for(const char* needed : { "version_number", "memory_size", "continuous_action_output_shape", "continuous_actions", "deterministic_continuous_actions" }) {
            if(std::find(outputs.begin(), outputs.end(), needed) == outputs.end()) throw PolicyError(std::string("no output ") + needed);
        }

        // The constants, from one evaluation
        std::vector<float> zeros(observation_size, 0.f);
        std::array<int64_t, 2> dims { 1, observation_size };
        Ort::Value tensor = Ort::Value::CreateTensor<float>(impl->memory, zeros.data(), zeros.size(), dims.data(), dims.size());
        const char* input_names[] = { "obs_0" };
        const char* constant_names[] = { "version_number", "memory_size", "continuous_action_output_shape" };
        auto constants = session.Run(Ort::RunOptions { nullptr }, input_names, &tensor, 1, constant_names, 3);
        model.version = (int) scalarOutput(constants[0]);
        model.memory_size = (int) scalarOutput(constants[1]);
        int actions = (int) scalarOutput(constants[2]);
        if(model.memory_size > 0) throw PolicyError("recurrent model (memory_size " + std::to_string(model.memory_size) + ")");
        if(actions != action_size) {
            throw PolicyError("the model outputs " + std::to_string(actions) + " continuous actions, the layout has " + std::to_string(action_size));
        }
        model.observation_size = observation_size;
        model.action_size = action_size;
    } catch(const Ort::Exception& e) {
        throw PolicyError(std::string("ONNX Runtime: ") + e.what());
    }

}

PolicyRunner::~PolicyRunner() = default;

void PolicyRunner::run(const float* obs, int batch, float* actions, bool deterministic) {

    std::array<int64_t, 2> dims { batch, model.observation_size };
    Ort::Value tensor = Ort::Value::CreateTensor<float>(impl->memory, const_cast<float*>(obs), (size_t) batch * model.observation_size, dims.data(), dims.size());
    const char* input_names[] = { "obs_0" };
    const char* output_names[] = { deterministic ? "deterministic_continuous_actions" : "continuous_actions" };
    auto out = impl->session.Run(Ort::RunOptions { nullptr }, input_names, &tensor, 1, output_names, 1);
    const float* data = out[0].GetTensorData<float>();
    std::copy(data, data + (size_t) batch * model.action_size, actions);

}
