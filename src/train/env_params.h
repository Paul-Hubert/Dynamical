#ifndef DY_ENV_PARAMS_H
#define DY_ENV_PARAMS_H

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dy {

// Environment parameters by source (mlagents-bridge.md): -set:<name>=<value> > the trainer's live value >
// the -envParams=<file> replay > the params-file default. P3 keeps the sources and the precedence; the accepted
// names and their effects come with the agent parameters (P10)
class EnvParamStore {
public:
    // Unity's envparams.txt (EnvParams.cs): '#' lines skipped, "name value" with any whitespace. A missing or
    // malformed file is fatal for the caller
    bool loadFile(const std::string& path, std::string& error);
    // -set:<name>=<value>
    bool setOverride(const std::string& name, const std::string& value, std::string& error);
    // Float values from the environment-parameters side channel
    void setLive(const std::string& name, float value);

    std::map<std::string, float> resolved() const;

private:
    std::map<std::string, float> file, live, overrides;
};

}

#endif
