#ifndef DY_AGENT_ENV_PARAMS_H
#define DY_AGENT_ENV_PARAMS_H

#include <map>
#include <string>

namespace dy {

// The resolved environment parameters as a world reads them at each episode start: a world context variable, written
// by the training host between decision windows. Absent outside training: agents keep their params file
struct AgentEnvParams {
    std::map<std::string, float> values;

    bool has(const std::string& name) const { return values.count(name) != 0; }
    float get(const std::string& name, float fallback) const {
        auto it = values.find(name);
        return it == values.end() ? fallback : it->second;
    }
};

}

#endif
