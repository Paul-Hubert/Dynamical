#ifndef DY_AGENT_POLICY_H
#define DY_AGENT_POLICY_H

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "logic/systems/system.h"
#include "policy_runner.h"

namespace dy {

struct AgentIO;

// An exported policy of any layout (docs/action-agents.md, inference), for agents that are components: where
// PolicyRunner takes one vector of observations and gives continuous actions, this one takes what the model asks for.
//   inputs  obs_0 [batch, n], obs_1 ... : the agent's vector observation, then its sensors in order; a buffer sensor
//           is [batch, rows, size]
//           action_masks [batch, sum of the branches]: all ones (models with discrete actions)
//   outputs (deterministic_)continuous_actions [batch, a], (deterministic_)discrete_actions [batch, branches]
// The model is checked against an agent at its first run; recurrent models are refused. run() is safe from several
// threads at once.
class AgentPolicy {
public:
    // Throws PolicyError with the reason
    explicit AgentPolicy(const std::filesystem::path& onnx, int threads = 1);
    ~AgentPolicy();

    // The agent's observation and sensors in, its action and discrete choices out. Throws PolicyError when the
    // agent's layout is not the model's
    void run(AgentIO& io, bool deterministic);

    const std::filesystem::path& path() const { return file; }
    int continuousActions() const { return continuous; }
    const std::vector<int>& branches() const { return discrete; }
    // "obs_0 [410], obs_1 [7] -> 168 continuous": for logs
    std::string describe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::filesystem::path file;
    int continuous = 0;
    std::vector<int> discrete;
};

// <dir>/<behavior>.onnx (what the trainer exports when a run ends), else the largest step
// <dir>/<behavior>/<behavior>-<step>.onnx (its checkpoints); empty without either
std::filesystem::path findBehaviorModel(const std::filesystem::path& dir, const std::string& behavior);

// Act phase, in a world whose agents are components: every agent that asked for a decision gets its behaviour's
// policy's action, as the training host would give it the trainer's (the request and the stats are consumed). A
// behaviour without a model is left alone: another system (a heuristic) or nobody answers it.
//
// params: "models" (a folder: a run of the trainer), "behaviors" ({ "<behavior>": "<file.onnx>" }, before the
//         folder), "prefix" (only behaviours whose name starts with it), "deterministic" (true)
class AgentPoliciesSys : public System {
public:
    AgentPoliciesSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "Policies"; }
    void tick(double h) override;

private:
    AgentPolicy* policyOf(const std::string& behavior);

    std::string models, prefix;
    std::map<std::string, std::string> files;
    bool deterministic = true;
    std::map<std::string, std::shared_ptr<AgentPolicy>> policies;   // null: looked for, none
};

void registerAgentPolicies();

}

#endif
