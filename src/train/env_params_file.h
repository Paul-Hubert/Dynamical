#ifndef DY_ENV_PARAMS_FILE_H
#define DY_ENV_PARAMS_FILE_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace dy {

// envparams.txt (-envParamsOut=), Unity's format (EnvParams.cs): a header
//   # run=<run dir> behavior=<name> written=<steps> [key=value ...]
// then "name value" lines in ordinal order, floats round-trippable. Extra header fields carry the
// engine's provenance (Unity's reader skips '#' lines). Written to a temporary file, then renamed over.
struct EnvParamsFile {
    std::string behavior;
    int64_t written = 0;
    std::map<std::string, std::string> header;     // sha, body, params, layout
    std::map<std::string, float> values;           // std::map<std::string> is ordinal
};

bool writeEnvParamsFile(const std::filesystem::path& path, const EnvParamsFile& file);

// Shortest decimal text that reads back to the same float
std::string roundTrip(float value);

}

#endif
