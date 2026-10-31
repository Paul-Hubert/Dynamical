#include <filesystem>
#include <memory>

#include "scene/run_args.h"
#include "scene/scenario_runner.h"
#include "scene/system_registry.h"
#include "physics/jolt_runtime.h"
#include "train/env_params_file.h"
#include "train/train_args.h"
#include "train/train_host.h"
#include "util/log.h"
#include "util/paths.h"

using namespace dy;

// dynamical_train (TrainingEnv.exe): headless, no Vulkan, SDL or OpenXR. It never changes the working directory
// (the trainer passes relative output paths) and never touches config.6.json
int main(int argc, char** argv) {

    TrainArgs train = parseTrainArgs(argc, argv);
    RunArgsResult run = parseRunArgs(train.rest);
    bool ok = true;
    for(auto& error : train.errors) { dy::log(dy::error) << error << "\n"; ok = false; }
    for(auto& key : run.unknown) { dy::log(dy::error) << "Unknown argument " << key << "\n"; ok = false; }
    for(auto& error : run.errors) { dy::log(dy::error) << error << "\n"; ok = false; }
    if(!ok) return 1;
    const RunArgs& args = run.args;

    JoltRuntime jolt;
    registerSimSystems();

    // Resources next to the executable: TrainingEnv.exe, resources/{scenes,prefabs,agents}
    SceneLoadOptions options;
    options.resources = executableDirectory() / "resources";
    options.allow_renderer = false;

    std::string scene = args.scene.empty() ? "walker_train" : args.scene;

    std::unique_ptr<ScenarioRunner> runner;
    try {
        runner = std::make_unique<ScenarioRunner>(scene, args.vars, options);
    } catch(const SceneError& e) {
        dy::log(dy::error) << "Scene: " << e.what() << "\n";
        return 1;
    }

    if(!train.log_file.empty()) {
        writeResolved(runner->scene(), std::filesystem::path(train.log_file).parent_path() / "scene.resolved.json");
    }

    if(!train.env_params_out.empty()) {
        EnvParamsFile file;
        file.behavior = "HeidelBotBehavior";
        if(!writeEnvParamsFile(train.env_params_out, file)) dy::log(dy::warning) << "Could not write " << train.env_params_out << "\n";
    }

    TrainHost host(args, train, *runner);
    return host.run();

}
