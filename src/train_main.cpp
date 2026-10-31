#include "scene/run_args.h"
#include "util/log.h"

// dynamical_train (TrainingEnv.exe): headless, no Vulkan, SDL or OpenXR
int main(int argc, char** argv) {

    auto parsed = dy::parseRunArgs(argc, argv);
    for(auto& key : parsed.unknown) dy::log(dy::error) << "Unknown argument " << key << "\n";
    for(auto& error : parsed.errors) dy::log(dy::error) << error << "\n";
    return parsed.unknown.empty() && parsed.errors.empty() ? 0 : 1;

}
