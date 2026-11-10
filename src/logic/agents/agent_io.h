#ifndef DY_AGENT_IO_H
#define DY_AGENT_IO_H

#include <entt/entt.hpp>

#include <string>
#include <utility>
#include <vector>

namespace dy {

// A value for the trainer's stats side channel. aggregation: 0 average, 1 most recent, 2 sum, 3 histogram
struct AgentStat {
    std::string key;
    float value = 0.f;
    int aggregation = 0;
    AgentStat(std::string key, float value, int aggregation = 0) : key(std::move(key)), value(value), aggregation(aggregation) {}
};

// The agent of a world as the training host sees it (one agent per world until the P8 runtime). World context
// variable, created by the agent's systems in preinit. The host writes `action` between decision windows and
// reads the rest after the Observe phase
struct AgentIO {
    std::string behavior;
    int observation_size = 0;
    int action_size = 0;
    std::string sensor_name;

    std::vector<float> action;          // host to world: the decision for the coming window (zeros when none)
    bool reset_requested = false;       // host to world: start a new episode at the next Observe phase (RESET)

    std::vector<float> observation;     // world to host, written by the Observe phase
    float reward = 0.f;                 // over the window that just ended
    bool done = false;                  // the episode ended with this observation
    bool truncated = false;             // ... by a limit (time, bounds), not by a terminal state
    std::vector<AgentStat> stats;       // this window's values, averaged over the agents by the host

    void configure(std::string behavior_name, int observations, int actions) {
        behavior = std::move(behavior_name);
        observation_size = observations;
        action_size = actions;
        sensor_name = "VectorSensor_size" + std::to_string(observations);
        action.assign(actions, 0.f);
        observation.assign(observations, 0.f);
    }
};

inline AgentIO& agentIO(entt::registry& reg) {
    if(auto* io = reg.ctx().find<AgentIO>()) return *io;
    return reg.ctx().emplace<AgentIO>();
}

}

#endif
