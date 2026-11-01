#ifndef DY_POLICY_SYSTEMS_H
#define DY_POLICY_SYSTEMS_H

#include "logic/systems/system.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

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

// What a world's policy is (ModelWatcher), for panels and overlays
struct PolicySource {
    std::string spec;               // the model parameter
    std::filesystem::path file;     // the loaded checkpoint, empty before the first load
    int64_t step = -1;              // from <behavior>-<step>.onnx, -1 when the name has none
    std::string error;
};

// Host context variable written by ModelWatcher: one PolicySource per world
struct PolicySources {
    std::vector<PolicySource> worlds;
    const PolicySource* find(size_t world) const { return world < worlds.size() ? &worlds[world] : nullptr; }
};

// Host system of the viewer: loads policies and injects them into the worlds (PolicyHandle).
// params: { "model": <spec> (every world), "models": { "<world index>": <spec> } (overrides), "poll": 2.0,
//           "deterministic": true }
// A spec is "<file.onnx>", or "latest:<dir>" for the <behavior>-<step>.onnx of the largest step in dir, polled for
// newer ones every `poll` seconds (wall clock; never in fixed-clock runs) and loaded once two polls see the same size
// and time. One path component of dir may be a pattern with '*' (latest:runs/*/WheelRoller): the newest checkpoint
// of the run whose newest checkpoint is the most recent. "|<file.onnx>" after a latest: spec is loaded while no
// checkpoint exists. Worlds with the same spec share one session; its layout comes from the first one's AgentIO and
// a model that does not match it is refused (PolicySource::error)
class ModelWatcherSys : public System {
public:
    ModelWatcherSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "ModelWatcher"; }
    void tick(double dt) override;

    // The newest <name>-<step>.onnx of dir (largest step; with a '*' component, of the most recent matching dir),
    // empty when there is none
    static std::filesystem::path latest(const std::filesystem::path& dir, int64_t* step = nullptr);

private:
    struct Feed {
        std::string spec;
        std::string dir;                // latest: specs
        std::string fallback;
        std::vector<size_t> worlds;
        std::filesystem::path file;     // loaded
        std::filesystem::path candidate;
        uintmax_t candidate_size = 0;
        std::filesystem::file_time_type candidate_time;
    };

    void load(Feed& feed, const std::filesystem::path& file, int64_t step);
    void first();       // on the first tick: host systems init before the worlds configure their agents

    bool started = false;

    std::string spec;
    std::map<size_t, std::string> overrides;
    double poll = 2.;
    bool deterministic = true;
    std::chrono::steady_clock::time_point last_poll;
    std::vector<Feed> feeds;
};

// Policy (world) and ModelWatcher (host). Called by the mains that link dynamical_onnx
void registerPolicySystems();

}

#endif
