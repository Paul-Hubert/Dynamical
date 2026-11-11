#ifndef DY_POLICY_RUNNER_H
#define DY_POLICY_RUNNER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace dy {

// An ML-Agents policy exported by the trainer (ONNX, opset 9), run on the CPU with ONNX Runtime
// (inference-and-replay.md). The fork's export (checked on the sphere roller's checkpoints):
//   input  obs_0 [batch, n]: raw observations, the normaliser is in the graph
//   output deterministic_continuous_actions [batch, a]: the mean, already clamp(-3, 3) / 3 as the trainer sends
//   output continuous_actions [batch, a]: mean + noise (RandomNormalLike), the same clamp
//   outputs version_number, memory_size, continuous_action_output_shape: constants
// The model is validated at load against the expected layout; recurrent models (memory_size > 0) are refused.
// run() is safe from several threads at once (Ort::Session::Run is).
class PolicyRunner {
public:
    struct Info {
        std::filesystem::path path;
        int observation_size = 0;
        int action_size = 0;
        int version = 0;
        int memory_size = 0;
    };

    // Throws PolicyError with the reason
    PolicyRunner(const std::filesystem::path& onnx, int observation_size, int action_size, int threads = 1);
    ~PolicyRunner();

    // obs: batch x observation_size floats, actions: batch x action_size floats
    void run(const float* obs, int batch, float* actions, bool deterministic = true);

    const Info& info() const { return model; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Info model;
};

struct PolicyError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class AgentPolicy;

// World context variable: the policy the world's Policy system runs. ModelWatcher (viewer) or a test injects it;
// replacing it swaps the policy at the next decision. `agent`: for an agent with more than a vector observation and
// continuous actions (a walker with its order sensor), instead of `runner`
struct PolicyHandle {
    std::shared_ptr<PolicyRunner> runner;
    bool deterministic = true;
    std::shared_ptr<AgentPolicy> agent;
};

// The handle for the world's agent: PolicyRunner for one vector of observations and continuous actions, AgentPolicy
// (agent_policy.h) otherwise. Throws PolicyError
struct AgentIO;
PolicyHandle makePolicyHandle(const std::filesystem::path& onnx, const AgentIO& io, bool deterministic = true);

}

#endif
