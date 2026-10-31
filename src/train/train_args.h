#ifndef DY_TRAIN_ARGS_H
#define DY_TRAIN_ARGS_H

#include <string>
#include <utility>
#include <vector>

namespace dy {

// The Unity-style flags the trainer passes to TrainingEnv.exe (mlagents-bridge.md, launch):
//   -nographics -batchmode --mlagents-port <port> -logFile <file>, then the config's env_args, with
//   -envParamsOut=<path> -gitSha=<sha> -envParams=<path> -set:<name>=<value>
// Anything else is a key=value run argument, returned in `rest`.
struct TrainArgs {
    int port = 0;                   // 0: no trainer
    std::string log_file;
    std::string env_params_out;
    std::string env_params_in;      // -envParams=
    std::string git_sha;
    std::vector<std::pair<std::string, std::string>> set;   // -set:<name>=<value>, in order
    std::vector<std::string> rest;
    std::vector<std::string> errors;
};

// Skips argv[0]
TrainArgs parseTrainArgs(int argc, char** argv);
TrainArgs parseTrainArgs(const std::vector<std::string>& args);

}

#endif
