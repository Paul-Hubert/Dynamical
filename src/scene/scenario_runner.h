#ifndef DY_SCENARIO_RUNNER_H
#define DY_SCENARIO_RUNNER_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "scene_def.h"
#include "world.h"

namespace dy {

// Loads a scene, creates and initialises its worlds and steps them headless (tests, dynamical_train).
// The renderer and host blocks are ignored: a world template that only uses library systems runs unchanged.
// Throws SceneError
class ScenarioRunner {
public:
    ScenarioRunner(const std::string& scene, const std::vector<std::pair<std::string, std::string>>& vars,
        const SceneLoadOptions& options, uint64_t base_seed = 0);
    ~ScenarioRunner();

    const SceneDef& scene() const { return def; }
    size_t worldCount() const { return worlds.size(); }
    World& world(size_t i) { return worlds[i]; }

    // The worlds again from scratch with another base seed (dynamical_train: the trainer's seed arrives after the
    // scene was loaded and checked)
    void restart(uint64_t base_seed);

    // Every world advances `ticks` fixed ticks, in parallel on the executor when given
    void step(int64_t ticks, tf::Executor* executor = nullptr);
    int64_t ticks() const { return ticks_done; }

private:
    SceneDef def;
    Worlds worlds;
    int64_t ticks_done = 0;
};

}

#endif
