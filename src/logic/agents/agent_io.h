#ifndef DY_AGENT_IO_H
#define DY_AGENT_IO_H

#include <entt/entt.hpp>

#include <algorithm>
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

// An observation next to an agent's vector one: another vector (rows 0, `size` floats), or a buffer of up to `rows`
// entities of `size` floats (ML-Agents' variable length observation: `count` rows filled, the others zero). The
// trainer takes a row of zeros for an empty one: every filled row needs a value that is not zero
struct AgentSensor {
    std::string name;
    int rows = 0;
    int size = 0;
    int count = 0;
    std::vector<float> data;

    void clear() {
        count = 0;
        std::fill(data.begin(), data.end(), 0.f);
    }
    // The next row of a buffer, null when it is full
    float* add() {
        if(count >= rows) return nullptr;
        return data.data() + (size_t) (count++) * size;
    }
};

// An agent as the training host sees it (docs/action-agents.md, several agents in a world). Either the one agent of
// a world, a context variable created by the agent's systems in preinit, or a component: every entity that has one
// is an agent. The host writes `action` and `discrete` between decision windows and reads the rest after the Observe
// phase
struct AgentIO {
    std::string behavior;
    int observation_size = 0;
    int action_size = 0;
    std::string sensor_name;
    std::vector<int> branches;          // sizes of the discrete action branches, next to the continuous actions

    std::vector<float> action;          // host to world: the decision for the coming window (zeros when none)
    std::vector<int> discrete;          // ... one choice per branch
    bool reset_requested = false;       // host to world: start a new episode at the next Observe phase (RESET)

    std::vector<float> observation;     // world to host, written by the Observe phase
    std::vector<AgentSensor> sensors;   // further observations, sent after the vector one in this order
    float reward = 0.f;                 // over the window that just ended (since its last decision for a component)
    bool done = false;                  // the episode ended with this observation
    bool truncated = false;             // ... by a limit (time, bounds), not by a terminal state
    bool recordable = true;             // false: this observation belongs to no episode (a demo's warm-up), recorders skip it
    std::vector<AgentStat> stats;       // this window's values, averaged over the agents by the host

    // Components only (the agent of a world asks every window). requested: the agent asks for a decision with this
    // observation; set by its systems at the Observe phase, cleared by the host when it sends the agent, with its
    // stats. An agent that is not sent keeps its last action. A done agent is sent whatever this says; its entity
    // then starts another episode or is destroyed at the next window
    bool requested = false;
    int id = -1;                        // given by the host, unique in the run; kept by an entity over its episodes

    void configure(std::string behavior_name, int observations, int actions) {
        behavior = std::move(behavior_name);
        observation_size = observations;
        action_size = actions;
        sensor_name = "VectorSensor_size" + std::to_string(observations);
        action.assign(actions, 0.f);
        observation.assign(observations, 0.f);
    }
    void addBranch(int size) {
        branches.push_back(size);
        discrete.push_back(0);
    }
    AgentSensor& addVector(std::string name, int size) {
        sensors.push_back({ std::move(name), 0, size, 0, std::vector<float>((size_t) size, 0.f) });
        return sensors.back();
    }
    AgentSensor& addBuffer(std::string name, int rows, int size) {
        sensors.push_back({ std::move(name), rows, size, 0, std::vector<float>((size_t) rows * size, 0.f) });
        return sensors.back();
    }
};

// World context flag of worlds whose agents are components: the host asks every agent for a fresh start at the next
// Observe phase (RESET). Created by the agents' systems in preinit, which is what tells the host the world has agents
// before any exists
struct AgentsReset {
    bool requested = false;
};

inline AgentIO& agentIO(entt::registry& reg) {
    if(auto* io = reg.ctx().find<AgentIO>()) return *io;
    return reg.ctx().emplace<AgentIO>();
}

}

#endif
