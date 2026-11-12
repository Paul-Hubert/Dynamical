#include "scenario_runner.h"

#include "util/profile.h"

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

void ScenarioRunner::restart(uint64_t base_seed) {
    worlds.finish();
    worlds.list.clear();
    worlds.create(def, base_seed);
    worlds.preinit();
    worlds.init();
    ticks_done = 0;
}

void ScenarioRunner::step(int64_t ticks, tf::Executor* executor) {
    {
        DY_ZONE_N("Step worlds");
        worlds.step(ticks, executor);
    }
    ticks_done += ticks;
    // Without a window a frame of the profiler is one of these : a decision window of every world
    DY_FRAME();
}
