#include "run_args.h"

#include <charconv>
#include <set>

using namespace dy;

namespace {

bool parseDouble(const std::string& text, double& out) {
    // Also accepts a fraction such as 1/60
    auto slash = text.find('/');
    if(slash != std::string::npos) {
        double num, den;
        if(!parseDouble(text.substr(0, slash), num) || !parseDouble(text.substr(slash + 1), den) || den == 0.) return false;
        out = num / den;
        return true;
    }
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc() && ptr == text.data() + text.size();
}

template<typename T>
bool parseInt(const std::string& text, T& out) {
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc() && ptr == text.data() + text.size();
}

bool parseBool(const std::string& text, bool& out) {
    if(text == "true" || text == "1") out = true;
    else if(text == "false" || text == "0") out = false;
    else return false;
    return true;
}

}

RunArgsResult dy::parseRunArgs(int argc, char** argv) {
    std::vector<std::string> args;
    for(int i = 1; i < argc; i++) args.emplace_back(argv[i]);
    return parseRunArgs(args);
}

RunArgsResult dy::parseRunArgs(const std::vector<std::string>& args) {

    RunArgsResult result;
    RunArgs& a = result.args;
    std::set<std::string> given;

    for(const std::string& arg : args) {

        auto equal = arg.find('=');
        std::string key = arg.substr(0, equal);
        std::string value = equal == std::string::npos ? std::string() : arg.substr(equal + 1);

        bool ok = true;
        if(key.rfind("var.", 0) == 0 && key.size() > 4) {
            a.vars.emplace_back(key.substr(4), value);
        } else if(key == "scene") a.scene = value;
        else if(key == "mode") a.mode = value;
        else if(key == "threads") ok = parseInt(value, a.threads) && a.threads >= 0;
        else if(key == "ticks") ok = parseInt(value, a.ticks) && a.ticks >= 0;
        else if(key == "episodes") ok = parseInt(value, a.episodes) && a.episodes >= 0;
        else if(key == "seed") ok = parseInt(value, a.seed) && a.seed >= 0;
        else if(key == "capture") a.capture = value;
        else if(key == "capture_dir") a.capture_dir = value;
        else if(key == "fixed_frame_dt") ok = parseDouble(value, a.fixed_frame_dt) && a.fixed_frame_dt >= 0.;
        else if(key == "window_hidden") ok = parseBool(value, a.window_hidden);
        else if(key == "exit_after_capture") ok = parseBool(value, a.exit_after_capture);
        else if(key == "gpu_times") ok = parseBool(value, a.gpu_times);
        else if(key == "ray_tracing") ok = parseBool(value, a.ray_tracing);
        else if(key == "probe") a.probe = value;
        else if(key == "probe_entities") a.probe_entities = value;
        else if(key == "probe_every") ok = parseInt(value, a.probe_every) && a.probe_every > 0;
        else {
            result.unknown.push_back(key);
            continue;
        }

        if(!ok) result.errors.push_back("bad value for " + key + ": '" + value + "'");
        given.insert(key);
    }

    if(!a.capture.empty() || !a.probe.empty()) {
        if(!given.count("fixed_frame_dt")) a.fixed_frame_dt = 1. / 60.;
        if(!given.count("window_hidden")) a.window_hidden = true;
    }

    return result;

}
