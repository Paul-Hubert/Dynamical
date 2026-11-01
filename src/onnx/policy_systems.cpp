#include "policy_systems.h"

#include <algorithm>
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

namespace {

const std::regex& numbered() {
    static const std::regex r(R"((.+)-(\d+)\.onnx)");
    return r;
}

int64_t stepOf(const std::filesystem::path& file) {
    std::smatch m;
    std::string name = file.filename().string();
    return std::regex_match(name, m, numbered()) ? std::stoll(m[2].str()) : -1;
}

// The largest-step <name>-<step>.onnx of one directory
std::filesystem::path newestIn(const std::filesystem::path& dir, int64_t* step) {
    std::filesystem::path best;
    int64_t best_step = -1;
    std::error_code ec;
    for(auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if(!entry.is_regular_file()) continue;
        int64_t s = stepOf(entry.path());
        if(s > best_step) {
            best_step = s;
            best = entry.path();
        }
    }
    if(step) *step = best_step;
    return best;
}

// A path component with '*' as a regex
std::regex globRegex(const std::string& pattern) {
    std::string r;
    for(char c : pattern) {
        if(c == '*') r += ".*";
        else if(std::string("\\^$.|?+()[]{}").find(c) != std::string::npos) r += std::string("\\") + c;
        else r += c;
    }
    return std::regex(r);
}

}

ModelWatcherSys::ModelWatcherSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    spec = params.value("model", std::string());
    if(auto it = params.find("models"); it != params.end() && it->is_object()) {
        for(auto& [index, value] : it->items()) overrides[(size_t) std::stoul(index)] = value.get<std::string>();
    }
    poll = params.value("poll", poll);
    deterministic = params.value("deterministic", deterministic);
    reg.ctx().emplace<PolicySources>();
}

std::filesystem::path ModelWatcherSys::latest(const std::filesystem::path& dir, int64_t* step) {

    // Up to the first component with '*', the matching directories, then the rest of the path in each
    std::filesystem::path base, rest;
    std::string pattern;
    for(auto& part : dir) {
        if(!pattern.empty()) rest /= part;
        else if(part.string().find('*') != std::string::npos) pattern = part.string();
        else base /= part;
    }
    if(pattern.empty()) return newestIn(dir, step);

    std::regex match = globRegex(pattern);
    std::filesystem::path best;
    int64_t best_step = -1;
    std::filesystem::file_time_type best_time;
    std::error_code ec;
    for(auto& entry : std::filesystem::directory_iterator(base.empty() ? std::filesystem::path(".") : base, ec)) {
        if(!entry.is_directory() || !std::regex_match(entry.path().filename().string(), match)) continue;
        int64_t s = -1;
        auto file = newestIn(entry.path() / rest, &s);
        if(file.empty()) continue;
        auto time = std::filesystem::last_write_time(file, ec);
        if(ec) continue;
        if(best.empty() || time > best_time) {
            best = file;
            best_step = s;
            best_time = time;
        }
    }
    if(step) *step = best_step;
    return best;

}

void ModelWatcherSys::first() {

    auto& sources = reg.ctx().get<PolicySources>();
    auto* worlds = reg.ctx().find<Worlds>();
    size_t count = worlds ? worlds->size() : 0;
    sources.worlds.assign(count, PolicySource {});

    // Worlds with the same spec share a feed
    for(size_t i = 0; i < count; i++) {
        auto it = overrides.find(i);
        const std::string& s = it != overrides.end() ? it->second : spec;
        sources.worlds[i].spec = s;
        if(s.empty()) continue;
        auto feed = std::find_if(feeds.begin(), feeds.end(), [&](const Feed& f) { return f.spec == s; });
        if(feed == feeds.end()) {
            Feed f;
            f.spec = s;
            if(s.rfind("latest:", 0) == 0) {
                f.dir = s.substr(7);
                if(auto bar = f.dir.find('|'); bar != std::string::npos) {
                    f.fallback = f.dir.substr(bar + 1);
                    f.dir = f.dir.substr(0, bar);
                }
            }
            feeds.push_back(f);
            feed = feeds.end() - 1;
        }
        feed->worlds.push_back(i);
    }

    for(auto& feed : feeds) {
        if(feed.dir.empty()) {
            load(feed, feed.spec, stepOf(feed.spec));
            continue;
        }
        int64_t step = -1;
        auto file = latest(feed.dir, &step);
        if(!file.empty()) load(feed, file, step);
        else if(!feed.fallback.empty()) load(feed, feed.fallback, stepOf(feed.fallback));
        else {
            for(size_t i : feed.worlds) sources.worlds[i].error = "no <behavior>-<step>.onnx in " + feed.dir;
            dy::log(dy::warning) << "ModelWatcher: no <behavior>-<step>.onnx in " << feed.dir << "\n";
        }
    }
    last_poll = std::chrono::steady_clock::now();

}

void ModelWatcherSys::load(Feed& feed, const std::filesystem::path& file, int64_t step) {

    auto& sources = reg.ctx().get<PolicySources>();
    auto& worlds = reg.ctx().get<Worlds>();
    auto fail = [&](const std::string& error) {
        for(size_t i : feed.worlds) sources.worlds[i].error = error;
        dy::log(dy::error) << "ModelWatcher: " << error << "\n";
    };
    auto* io = worlds[feed.worlds.front()].reg.ctx().find<AgentIO>();
    if(!io || io->observation_size == 0) return fail("world " + std::to_string(feed.worlds.front()) + " has no agent");

    try {
        auto runner = std::make_shared<PolicyRunner>(file, io->observation_size, io->action_size);
        for(size_t i : feed.worlds) {
            worlds[i].reg.ctx().insert_or_assign(PolicyHandle { runner, deterministic });
            auto& source = sources.worlds[i];
            source.file = file;
            source.step = step;
            source.error.clear();
        }
        feed.file = file;
        dy::log(dy::info) << "ModelWatcher: " << file.string() << " (step " << step << ", version " << runner->info().version << ") in "
            << feed.worlds.size() << " worlds\n";
    } catch(const PolicyError& e) {
        fail(file.filename().string() + ": " + e.what());
    }

}

void ModelWatcherSys::tick(double dt) {

    if(!started) {
        started = true;
        first();
        return;
    }
    if(std::none_of(feeds.begin(), feeds.end(), [](const Feed& f) { return !f.dir.empty(); })) return;
    if(auto* args = reg.ctx().find<RunArgs>(); args && args->deterministic()) return;
    auto now = std::chrono::steady_clock::now();
    if(std::chrono::duration<double>(now - last_poll).count() < poll) return;
    last_poll = now;

    for(auto& feed : feeds) {
        if(feed.dir.empty()) continue;
        int64_t step = -1;
        auto file = latest(feed.dir, &step);
        if(file.empty() || file == feed.file) continue;

        // A file the trainer may still be writing: load it once two polls agree on its size and time
        std::error_code ec;
        auto size = std::filesystem::file_size(file, ec);
        auto time = std::filesystem::last_write_time(file, ec);
        if(ec) continue;
        if(file == feed.candidate && size == feed.candidate_size && time == feed.candidate_time) {
            load(feed, file, step);
            feed.candidate.clear();
        } else {
            feed.candidate = file;
            feed.candidate_size = size;
            feed.candidate_time = time;
        }
    }

}



void dy::registerPolicySystems() {
    registerSystem("Policy", systemInfo<PolicySys>(SystemScope::World));
    registerSystem("ModelWatcher", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<ModelWatcherSys>(reg, params); },
        SystemScope::Host });
}
