#include "scenario_runner.h"

using namespace dy;

ScenarioRunner::ScenarioRunner(const std::string& scene, const std::vector<std::pair<std::string, std::string>>& vars, const SceneLoadOptions& options)
    : def(loadScene(scene, vars, options)) {

}

void ScenarioRunner::step(int64_t ticks) {
    ticks_done += ticks;
}
