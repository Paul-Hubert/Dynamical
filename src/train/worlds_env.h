#ifndef DY_WORLDS_ENV_H
#define DY_WORLDS_ENV_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "environment.h"
#include "logic/agents/agent_io.h"
#include "scene/scenario_runner.h"

namespace tf { class Executor; }

namespace dy {

// mode=train on a scene of agent worlds. A world's agents are its AgentIO context variable (sphere_roll, the walkers:
// one agent per world, a decision every window) and every entity with an AgentIO component (pursuit, the sandbox:
// agents of several behaviours that are born, die and ask for decisions at their own rate; docs/action-agents.md).
// Every STEP runs decision windows of every world, in parallel on `threads` workers, until an agent asks for a
// decision or ends. A done agent gets no action: it starts its next episode, or leaves, with the next window
class WorldsEnvironment : public TrainerEnvironment {
public:
    WorldsEnvironment(ScenarioRunner& runner, unsigned threads);
    ~WorldsEnvironment() override;

    // Empty when every world has agents, the worlds share a decision period and the agents of a behaviour a layout
    static std::string check(ScenarioRunner& runner);

    // The behaviours of the scene's agents as they stand, in name order (a world of components may hold more later)
    static std::vector<std::string> behaviors(ScenarioRunner& runner);

    void reset() override;
    bool step(const mlagents::co::UnityRLInputProto& input, std::string& error) override;
    void write(mlagents::co::UnityOutputProto& output) override;

    // "4 Chaser (6 + 5x6 observations, 2 + [2] actions), 8 Runner (...)": the agents the worlds hold now
    std::string describe() const;
    uint32_t decisionPeriod() const { return period; }

private:
    // An agent: the one of a world (entity null) or a component
    struct Slot {
        uint32_t world = 0;
        entt::entity entity = entt::null;
    };

    struct Group {
        // The layout of the behaviour, from its first agent
        int observation_size = 0, action_size = 0;
        std::vector<int> branches;
        std::vector<std::pair<int, int>> sensors;   // rows, size
        std::string layout;                         // for describe()
        bool brain_sent = false;

        std::vector<Slot> pending;                  // ask for a decision or ended, not written yet
        std::vector<Slot> sent;                     // the last output's, in its order
        std::vector<char> done;                     // ... which of them ended
    };

    AgentIO* find(const Slot& slot);
    // The agents that ask for a decision or ended, into their behaviour's `pending`. false with `problem` when an
    // agent does not have its behaviour's layout
    bool gather();
    size_t pendingCount() const;

    ScenarioRunner& runner;
    std::unique_ptr<tf::Executor> executor;
    std::map<std::string, Group> groups;
    uint32_t period = 1;
    int32_t next_id = 0;
    std::string problem;
};

}

#endif
