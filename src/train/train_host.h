#ifndef DY_TRAIN_HOST_H
#define DY_TRAIN_HOST_H

#include <atomic>
#include <cstdint>

#include "env_params.h"
#include "scene/run_args.h"
#include "scene/scenario_runner.h"
#include "train_args.h"

namespace dy {

// dynamical_train's loop: no frame loop, the worlds step as fast as the CPU allows (simulation.md, training host)
class TrainHost {
public:
    TrainHost(const RunArgs& args, const TrainArgs& train, ScenarioRunner& runner) : args(args), train(train), runner(runner) {}

    // Runs the mode, returns the process exit code: 0 on QUIT, header 400 or a stop signal; 1 on bad arguments;
    // 2 when the connection is lost otherwise
    int run();

    // The seed of the trainer's initialisation (it already includes the worker id: learn.py env_seed)
    int64_t baseSeed() const { return base_seed.load(); }

private:
    // mode=bench: every world steps `ticks` ticks on `threads` workers, ticks/s logged
    int bench();

    // mode=null: the trainer handshake and step loop with fixed observations (M0)
    int null();

    // -envParams= and -set:, before anything else (a missing file is fatal, as in Unity)
    bool loadEnvParams();
    void writeEnvParams(int64_t steps);

    const RunArgs& args;
    const TrainArgs& train;
    ScenarioRunner& runner;
    EnvParamStore env_params;
    std::atomic<int64_t> base_seed { -1 };
    std::map<std::string, float> written;
    bool envparams_written = false;
};

}

#endif
