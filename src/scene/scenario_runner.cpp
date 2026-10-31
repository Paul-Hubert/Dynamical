#include "scenario_runner.h"

using namespace dy;

ScenarioRunner::ScenarioRunner(const std::string& scene, const std::vector<std::pair<std::string, std::string>>& vars,
    const SceneLoadOptions& options, uint64_t base_seed) : def(loadScene(scene, vars, options)) {

    worlds.create(def, base_seed);
    worlds.preinit();
    worlds.init();

}

ScenarioRunner::~ScenarioRunner() {
    worlds.finish();
}

void ScenarioRunner::step(int64_t ticks, tf::Executor* executor) {
    worlds.step(ticks, executor);
    ticks_done += ticks;
}
