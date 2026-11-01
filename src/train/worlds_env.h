#ifndef DY_WORLDS_ENV_H
#define DY_WORLDS_ENV_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "environment.h"
#include "logic/agents/agent_io.h"
#include "scene/scenario_runner.h"

namespace tf { class Executor; }

namespace dy {

// mode=train on a scene of agent worlds (sphere_roll): one agent per world (its AgentIO context variable), all of
// one behaviour, agent id = world index. Every STEP runs one decision window of every world, in parallel on
// `threads` workers. A done agent gets no action: its world starts the next episode with the next window
class WorldsEnvironment : public TrainerEnvironment {
public:
    WorldsEnvironment(ScenarioRunner& runner, unsigned threads);
    ~WorldsEnvironment() override;

    // Empty when every world has an AgentIO of the same behaviour, sizes and decision period
    static std::string check(ScenarioRunner& runner);

    void reset() override;
    bool step(const mlagents::co::UnityRLInputProto& input, std::string& error) override;
    void write(mlagents::co::UnityOutputProto& output) override;

    const AgentIO& agent(size_t i) const { return *agents[i]; }
    size_t size() const { return agents.size(); }
    const std::string& behavior() const { return agents[0]->behavior; }
    uint32_t decisionPeriod() const { return period; }

private:
    ScenarioRunner& runner;
    std::unique_ptr<tf::Executor> executor;
    std::vector<AgentIO*> agents;
    std::vector<char> done;             // per agent, in the last output
    uint32_t period = 1;
    bool brain_sent = false;
};

}

#endif
