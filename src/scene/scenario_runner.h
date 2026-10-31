#ifndef DY_SCENARIO_RUNNER_H
#define DY_SCENARIO_RUNNER_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "scene_def.h"

namespace dy {

// Loads a scene and steps its worlds headless (tests, dynamical_train). The renderer and host blocks are ignored:
// a world template that only uses library systems runs unchanged. Throws SceneError
class ScenarioRunner {
public:
    ScenarioRunner(const std::string& scene, const std::vector<std::pair<std::string, std::string>>& vars, const SceneLoadOptions& options);

    const SceneDef& scene() const { return def; }
    size_t worldCount() const { return 0; }

    // Every world advances `ticks` fixed ticks
    void step(int64_t ticks);
    int64_t ticks() const { return ticks_done; }

private:
    SceneDef def;
    int64_t ticks_done = 0;
};

}

#endif
