#include "agent_policy.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <regex>

#include "logic/agents/agent_io.h"
#include "scene/system_registry.h"
#include "util/log.h"

using namespace dy;

namespace {

Ort::Env& env() {
    static Ort::Env instance(ORT_LOGGING_LEVEL_WARNING, "dynamical-agents");
    return instance;
}

// One session per file, shared by the worlds that use it
std::shared_ptr<AgentPolicy> shared(const std::filesystem::path& file) {
    static std::mutex mutex;
    static std::map<std::string, std::weak_ptr<AgentPolicy>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    std::error_code ec;
    const std::string key = std::filesystem::weakly_canonical(file, ec).string() + "@" + std::to_string(std::filesystem::last_write_time(file, ec).time_since_epoch().count());
    if(auto existing = cache[key].lock()) return existing;
    auto made = std::make_shared<AgentPolicy>(file);
    cache[key] = made;
    return made;
}

}

struct AgentPolicy::Impl {
    Ort::Session session { nullptr };
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    struct Input {
        std::string name;
        int observation = -1;           // obs_<n>; -1: the action masks
        std::vector<int64_t> shape;     // without the batch
        size_t size = 0;
    };
    std::vector<Input> inputs;
    bool has_continuous = false, has_discrete = false;
    std::vector<float> masks;
};

AgentPolicy::AgentPolicy(const std::filesystem::path& onnx, int threads) : impl(std::make_unique<Impl>()), file(onnx) {

    if(!std::filesystem::exists(onnx)) throw PolicyError("no file " + onnx.string());
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(threads);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        impl->session = Ort::Session(env(), onnx.c_str(), options);
        auto& session = impl->session;
        Ort::AllocatorWithDefaultOptions allocator;

        for(size_t i = 0; i < session.GetInputCount(); i++) {
            Impl::Input input;
            input.name = session.GetInputNameAllocated(i, allocator).get();
            auto shape = session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
            if(shape.size() < 2) throw PolicyError("input " + input.name + " has no batch dimension");
            input.shape.assign(shape.begin() + 1, shape.end());
            input.size = 1;
            for(int64_t d : input.shape) {
                if(d <= 0) throw PolicyError("input " + input.name + " has a free dimension");
                input.size *= (size_t) d;
            }
            if(input.name.rfind("obs_", 0) == 0) input.observation = std::stoi(input.name.substr(4));
            else if(input.name != "action_masks") throw PolicyError("unexpected input " + input.name + " (a recurrent model?)");
            impl->inputs.push_back(std::move(input));
        }

        std::vector<std::string> outputs;
        for(size_t i = 0; i < session.GetOutputCount(); i++) outputs.push_back(session.GetOutputNameAllocated(i, allocator).get());
        auto has = [&](const char* name) { return std::find(outputs.begin(), outputs.end(), name) != outputs.end(); };
        impl->has_continuous = has("continuous_actions") && has("deterministic_continuous_actions");
        impl->has_discrete = has("discrete_actions") && has("deterministic_discrete_actions");
        if(!impl->has_continuous && !impl->has_discrete) throw PolicyError("no action output");
        for(auto& input : impl->inputs) if(input.observation < 0) impl->masks.assign(input.size, 1.f);

        // The sizes of the actions, from one evaluation on zeros
        std::vector<std::vector<float>> zeros;
        std::vector<Ort::Value> tensors;
        std::vector<const char*> input_names;
        for(auto& input : impl->inputs) {
            zeros.emplace_back(input.size, input.observation < 0 ? 1.f : 0.f);
            std::vector<int64_t> dims { 1 };
            dims.insert(dims.end(), input.shape.begin(), input.shape.end());
            tensors.push_back(Ort::Value::CreateTensor<float>(impl->memory, zeros.back().data(), zeros.back().size(), dims.data(), dims.size()));
            input_names.push_back(input.name.c_str());
        }
        std::vector<const char*> output_names;
        if(impl->has_continuous) output_names.push_back("deterministic_continuous_actions");
        if(impl->has_discrete) output_names.push_back("deterministic_discrete_actions");
        auto out = session.Run(Ort::RunOptions { nullptr }, input_names.data(), tensors.data(), tensors.size(), output_names.data(), output_names.size());
        size_t k = 0;
        if(impl->has_continuous) continuous = (int) out[k++].GetTensorTypeAndShapeInfo().GetShape().back();
        if(impl->has_discrete) {
            const int branches = (int) out[k++].GetTensorTypeAndShapeInfo().GetShape().back();
            // Their sizes are not in the graph's outputs: the masks' total, which run() checks against the agent's
            discrete.assign((size_t) branches, 0);
        }
    } catch(const Ort::Exception& e) {
        throw PolicyError(std::string("ONNX Runtime: ") + e.what());
    }

}

AgentPolicy::~AgentPolicy() = default;

std::string AgentPolicy::describe() const {
    std::string s;
    for(auto& input : impl->inputs) {
        if(input.observation < 0) continue;
        s += (s.empty() ? "" : ", ") + input.name + " [";
        for(size_t d = 0; d < input.shape.size(); d++) s += (d ? " x " : "") + std::to_string(input.shape[d]);
        s += "]";
    }
    s += " -> " + std::to_string(continuous) + " continuous";
    if(!discrete.empty()) s += ", " + std::to_string(discrete.size()) + " discrete";
    return s;
}

void AgentPolicy::run(AgentIO& io, bool deterministic) {

    std::vector<Ort::Value> tensors;
    std::vector<const char*> input_names;
    for(auto& input : impl->inputs) {
        const float* data = nullptr;
        size_t size = 0;
        if(input.observation < 0) {
            int total = 0;
            for(int b : io.branches) total += b;
            if((size_t) total != input.size) throw PolicyError(file.filename().string() + ": the model's branches total " + std::to_string(input.size) + ", the agent's " + std::to_string(total));
            data = impl->masks.data();
            size = impl->masks.size();
        } else if(input.observation == 0) {
            data = io.observation.data();
            size = io.observation.size();
        } else if((size_t) input.observation <= io.sensors.size()) {
            data = io.sensors[(size_t) input.observation - 1].data.data();
            size = io.sensors[(size_t) input.observation - 1].data.size();
        }
        if(size != input.size) {
            throw PolicyError(file.filename().string() + ": " + input.name + " takes " + std::to_string(input.size) + " floats, the agent of " + io.behavior + " has " + std::to_string(size));
        }
        std::vector<int64_t> dims { 1 };
        dims.insert(dims.end(), input.shape.begin(), input.shape.end());
        tensors.push_back(Ort::Value::CreateTensor<float>(impl->memory, const_cast<float*>(data), size, dims.data(), dims.size()));
        input_names.push_back(input.name.c_str());
    }
    if(continuous != io.action_size || discrete.size() != io.branches.size()) {
        throw PolicyError(file.filename().string() + ": the model acts with " + std::to_string(continuous) + " + " + std::to_string(discrete.size())
            + " branches, the agent of " + io.behavior + " with " + std::to_string(io.action_size) + " + " + std::to_string(io.branches.size()));
    }

    std::vector<const char*> output_names;
    if(impl->has_continuous) output_names.push_back(deterministic ? "deterministic_continuous_actions" : "continuous_actions");
    if(impl->has_discrete) output_names.push_back(deterministic ? "deterministic_discrete_actions" : "discrete_actions");
    try {
        auto out = impl->session.Run(Ort::RunOptions { nullptr }, input_names.data(), tensors.data(), tensors.size(), output_names.data(), output_names.size());
        size_t k = 0;
        if(impl->has_continuous) {
            const float* data = out[k++].GetTensorData<float>();
            std::copy(data, data + continuous, io.action.begin());
        }
        if(impl->has_discrete) {
            auto& value = out[k++];
            const auto type = value.GetTensorTypeAndShapeInfo().GetElementType();
            for(size_t b = 0; b < io.branches.size(); b++) {
                int64_t choice = 0;
                if(type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) choice = value.GetTensorData<int64_t>()[b];
                else if(type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) choice = value.GetTensorData<int32_t>()[b];
                else if(type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) choice = (int64_t) value.GetTensorData<float>()[b];
                io.discrete[b] = (int) std::clamp<int64_t>(choice, 0, io.branches[b] - 1);
            }
        }
    } catch(const Ort::Exception& e) {
        throw PolicyError(file.filename().string() + ": ONNX Runtime: " + e.what());
    }

}

std::filesystem::path dy::findBehaviorModel(const std::filesystem::path& dir, const std::string& behavior) {
    std::error_code ec;
    std::filesystem::path direct = dir / (behavior + ".onnx");
    if(std::filesystem::exists(direct, ec)) return direct;
    std::filesystem::path best;
    long long best_step = -1;
    const std::regex numbered(R"((.+)-(\d+)\.onnx)");
    for(auto& entry : std::filesystem::directory_iterator(dir / behavior, ec)) {
        if(!entry.is_regular_file()) continue;
        std::smatch m;
        const std::string name = entry.path().filename().string();
        if(!std::regex_match(name, m, numbered)) continue;
        const long long step = std::stoll(m[2].str());
        if(step > best_step) {
            best_step = step;
            best = entry.path();
        }
    }
    return best;
}

AgentPoliciesSys::AgentPoliciesSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    models = params.value("models", std::string());
    prefix = params.value("prefix", std::string());
    deterministic = params.value("deterministic", deterministic);
    if(auto it = params.find("behaviors"); it != params.end() && it->is_object()) {
        for(auto& [behavior, file] : it->items()) files[behavior] = file.get<std::string>();
    }
}

AgentPolicy* AgentPoliciesSys::policyOf(const std::string& behavior) {
    auto found = policies.find(behavior);
    if(found != policies.end()) return found->second.get();
    std::shared_ptr<AgentPolicy> policy;
    if(prefix.empty() || behavior.rfind(prefix, 0) == 0) {
        std::filesystem::path file;
        if(auto it = files.find(behavior); it != files.end()) file = it->second;
        else if(!models.empty()) file = findBehaviorModel(models, behavior);
        if(!file.empty()) {
            try {
                policy = shared(file);
                dy::log(dy::info) << "Policies: " << behavior << " from " << file.string() << " (" << policy->describe() << ")\n";
            } catch(const PolicyError& e) {
                dy::log(dy::error) << "Policies: " << behavior << ": " << e.what() << "\n";
            }
        } else if(!models.empty() || !files.empty()) {
            dy::log(dy::debug) << "Policies: no model for " << behavior << "\n";
        }
    }
    policies[behavior] = policy;
    return policy.get();
}

void AgentPoliciesSys::tick(double h) {
    for(auto [e, io] : reg.view<AgentIO>().each()) {
        if(io.done) {
            io.requested = false;
            continue;
        }
        if(!io.requested) continue;
        AgentPolicy* policy = policyOf(io.behavior);
        if(!policy) continue;
        try {
            policy->run(io, deterministic);
        } catch(const PolicyError& e) {
            dy::log(dy::error) << "Policies: " << e.what() << "\n";
            policies[io.behavior] = nullptr;
            continue;
        }
        io.requested = false;
        io.stats.clear();
    }
}

void dy::registerAgentPolicies() {
    registerSystem("Policies", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<AgentPoliciesSys>(reg, params); },
        SystemScope::World });
}

PolicyHandle dy::makePolicyHandle(const std::filesystem::path& onnx, const AgentIO& io, bool deterministic) {
    PolicyHandle handle;
    handle.deterministic = deterministic;
    if(io.sensors.empty() && io.branches.empty()) handle.runner = std::make_shared<PolicyRunner>(onnx, io.observation_size, io.action_size);
    else handle.agent = shared(onnx);
    return handle;
}
