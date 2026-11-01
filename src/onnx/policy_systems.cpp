#include "policy_systems.h"

#include <regex>

#include "logic/agents/agent_io.h"
#include "policy_runner.h"
#include "scene/run_args.h"
#include "scene/system_registry.h"
#include "scene/world.h"
#include "util/log.h"

using namespace dy;



// Policy

void PolicySys::tick(double h) {
    auto* handle = reg.ctx().find<PolicyHandle>();
    auto* io = reg.ctx().find<AgentIO>();
    if(!handle || !handle->runner || !io) return;
    const auto& info = handle->runner->info();
    if((int) io->observation.size() != info.observation_size || (int) io->action.size() != info.action_size) return;
    handle->runner->run(io->observation.data(), 1, io->action.data(), handle->deterministic);
}



// ModelWatcher

ModelWatcherSys::ModelWatcherSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    spec = params.value("model", std::string());
    poll = params.value("poll", poll);
    deterministic = params.value("deterministic", deterministic);
    reg.ctx().emplace<PolicySource>().spec = spec;
}

std::filesystem::path ModelWatcherSys::latest(const std::filesystem::path& dir, int64_t* step) {
    static const std::regex numbered(R"(.+-(\d+)\.onnx)");
    std::filesystem::path best;
    int64_t best_step = -1;
    std::error_code ec;
    for(auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::smatch m;
        std::string name = entry.path().filename().string();
        if(!entry.is_regular_file() || !std::regex_match(name, m, numbered)) continue;
        int64_t s = std::stoll(m[1].str());
        if(s > best_step) {
            best_step = s;
            best = entry.path();
        }
    }
    if(step) *step = best_step;
    return best;
}

void ModelWatcherSys::first() {
    auto& source = reg.ctx().get<PolicySource>();
    if(spec.rfind("latest:", 0) == 0) {
        int64_t step = -1;
        auto file = latest(spec.substr(7), &step);
        if(file.empty()) {
            source.error = "no <behavior>-<step>.onnx in " + spec.substr(7);
            dy::log(dy::warning) << "ModelWatcher: " << source.error << "\n";
            return;
        }
        load(file, step);
    } else if(!spec.empty()) {
        static const std::regex numbered(R"(.+-(\d+)\.onnx)");
        std::smatch m;
        std::string name = std::filesystem::path(spec).filename().string();
        load(spec, std::regex_match(name, m, numbered) ? std::stoll(m[1].str()) : -1);
    }
    last_poll = std::chrono::steady_clock::now();
}

void ModelWatcherSys::load(const std::filesystem::path& file, int64_t step) {

    auto& source = reg.ctx().get<PolicySource>();
    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || worlds->size() == 0) {
        source.error = "the scene has no worlds";
        return;
    }
    auto* io = (*worlds)[0].reg.ctx().find<AgentIO>();
    if(!io || io->observation_size == 0) {
        source.error = "world 0 has no agent";
        return;
    }

    try {
        auto runner = std::make_shared<PolicyRunner>(file, io->observation_size, io->action_size);
        for(size_t i = 0; i < worlds->size(); i++) (*worlds)[i].reg.ctx().insert_or_assign(PolicyHandle { runner, deterministic });
        source.file = file;
        source.step = step;
        source.error.clear();
        dy::log(dy::info) << "ModelWatcher: " << file.string() << " (step " << step << ", version " << runner->info().version << ") in "
            << worlds->size() << " worlds\n";
    } catch(const PolicyError& e) {
        source.error = file.filename().string() + ": " + e.what();
        dy::log(dy::error) << "ModelWatcher: " << source.error << "\n";
    }

}

void ModelWatcherSys::tick(double dt) {

    if(!started) {
        started = true;
        first();
        return;
    }
    if(spec.rfind("latest:", 0) != 0) return;
    if(auto* args = reg.ctx().find<RunArgs>(); args && args->deterministic()) return;
    auto now = std::chrono::steady_clock::now();
    if(std::chrono::duration<double>(now - last_poll).count() < poll) return;
    last_poll = now;

    int64_t step = -1;
    auto file = latest(spec.substr(7), &step);
    auto& source = reg.ctx().get<PolicySource>();
    if(file.empty() || file == source.file) return;

    // A file the trainer may still be writing: load it once two polls agree on its size and time
    std::error_code ec;
    auto size = std::filesystem::file_size(file, ec);
    auto time = std::filesystem::last_write_time(file, ec);
    if(ec) return;
    if(file == candidate && size == candidate_size && time == candidate_time) {
        load(file, step);
        candidate.clear();
    } else {
        candidate = file;
        candidate_size = size;
        candidate_time = time;
    }

}



void dy::registerPolicySystems() {
    registerSystem("Policy", systemInfo<PolicySys>(SystemScope::World));
    registerSystem("ModelWatcher", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<ModelWatcherSys>(reg, params); },
        SystemScope::Host });
}
