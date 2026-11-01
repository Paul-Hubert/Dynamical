#ifndef DY_POLICY_SYSTEMS_H
#define DY_POLICY_SYSTEMS_H

#include "logic/systems/system.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace dy {

class PolicyRunner;

// World system, Act phase (the slot of TrainerActions in a training template): the world's PolicyHandle on its
// AgentIO observation gives the window's action. Nothing happens without a handle (the action stays as it is)
class PolicySys : public System {
public:
    PolicySys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Policy"; }
    void tick(double h) override;
};

// Host context variable written by ModelWatcher, for panels and overlays
struct PolicySource {
    std::string spec;               // the model parameter
    std::filesystem::path file;     // the loaded checkpoint, empty before the first load
    int64_t step = -1;              // from <behavior>-<step>.onnx, -1 when the name has none
    std::string error;
};

// Host system of the viewer: loads a policy and injects it into every world (PolicyHandle).
// params: { "model": "<file.onnx>" or "latest:<dir>", "poll": 2.0, "deterministic": true }
// latest:<dir> takes the <behavior>-<step>.onnx of the largest step in dir and polls for newer ones every `poll`
// seconds (wall clock; never in fixed-clock runs), loading a file only once two polls see the same size and time.
// The layout comes from world 0's AgentIO; a model that does not match it is refused (PolicySource::error)
class ModelWatcherSys : public System {
public:
    ModelWatcherSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "ModelWatcher"; }
    void tick(double dt) override;

    // The newest <name>-<step>.onnx of dir (largest step), empty when there is none
    static std::filesystem::path latest(const std::filesystem::path& dir, int64_t* step = nullptr);

private:
    void load(const std::filesystem::path& file, int64_t step);
    void first();       // on the first tick: host systems init before the worlds configure their agents

    bool started = false;

    std::string spec;
    double poll = 2.;
    bool deterministic = true;
    std::chrono::steady_clock::time_point last_poll;
    std::filesystem::path candidate;
    uintmax_t candidate_size = 0;
    std::filesystem::file_time_type candidate_time;
};

// Policy (world) and ModelWatcher (host). Called by the mains that link dynamical_onnx
void registerPolicySystems();

}

#endif
