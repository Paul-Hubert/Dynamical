#include "train_args.h"

#include <charconv>

using namespace dy;

TrainArgs dy::parseTrainArgs(int argc, char** argv) {
    std::vector<std::string> args;
    for(int i = 1; i < argc; i++) args.emplace_back(argv[i]);
    return parseTrainArgs(args);
}

TrainArgs dy::parseTrainArgs(const std::vector<std::string>& args) {

    TrainArgs t;

    auto startsWith = [](const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; };

    for(size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        bool has_next = i + 1 < args.size();

        if(a == "-nographics" || a == "-batchmode") {
            // Always headless
        } else if(a == "--mlagents-port") {
            if(!has_next) {
                t.errors.push_back("--mlagents-port needs a port");
                continue;
            }
            const std::string& v = args[++i];
            auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), t.port);
            if(ec != std::errc() || ptr != v.data() + v.size() || t.port <= 0 || t.port > 65535) t.errors.push_back("bad port '" + v + "'");
        } else if(a == "-logFile") {
            if(!has_next) {
                t.errors.push_back("-logFile needs a file");
                continue;
            }
            t.log_file = args[++i];
        } else if(startsWith(a, "-envParamsOut=")) {
            t.env_params_out = a.substr(14);
        } else if(startsWith(a, "-envParams=")) {
            t.env_params_in = a.substr(11);
        } else if(startsWith(a, "-gitSha=")) {
            t.git_sha = a.substr(8);
        } else if(startsWith(a, "-set:")) {
            auto equal = a.find('=');
            if(equal == std::string::npos || equal == 5) {
                t.errors.push_back("expected -set:<name>=<value>, got " + a);
                continue;
            }
            t.set.emplace_back(a.substr(5, equal - 5), a.substr(equal + 1));
        } else if(startsWith(a, "-")) {
            t.errors.push_back("unknown flag " + a);
        } else {
            t.rest.push_back(a);
        }
    }

    return t;

}
