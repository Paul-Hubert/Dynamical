#include <filesystem>
#include <memory>

#include "scene/run_args.h"
#include "scene/scenario_runner.h"
#include "scene/system_registry.h"
#include "physics/jolt_runtime.h"
#include "train/train_args.h"
#include "train/process_control.h"
#include "train/train_host.h"
#include "util/log.h"
#include "util/paths.h"

using namespace dy;

// dynamical_train (TrainingEnv.exe): headless, no Vulkan, SDL or OpenXR. It never changes the working directory
// (the trainer passes relative output paths) and never touches config.6.json
int main(int argc, char** argv) {

    // stop-run's CTRL_C, and the log file the trainer greps, before any other output
    installStopHandler();
    TrainArgs train = parseTrainArgs(argc, argv);
    if(!train.log_file.empty() && !setLogFile(train.log_file)) {
        dy::log(dy::error) << "Cannot open the log file " << train.log_file << "\n";
        return 1;
    }
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

    // An unmodified trainer config passes no scene: walker_train. mode=null runs the null scene
    std::string scene = !args.scene.empty() ? args.scene : args.mode == "null" ? "null" : "walker_train";

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

    TrainHost host(args, train, *runner);
    return host.run();

}
