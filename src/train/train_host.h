#ifndef DY_TRAIN_HOST_H
#define DY_TRAIN_HOST_H

#include "scene/run_args.h"
#include "scene/scenario_runner.h"
#include "train_args.h"

namespace dy {

// dynamical_train's loop: no frame loop, the worlds step as fast as the CPU allows (simulation.md, training host)
class TrainHost {
public:
    TrainHost(const RunArgs& args, const TrainArgs& train, ScenarioRunner& runner) : args(args), train(train), runner(runner) {}

    // Runs the mode, returns the process exit code
    int run();

private:
    // mode=bench: every world steps `ticks` ticks on `threads` workers, ticks/s logged
    int bench();

    const RunArgs& args;
    const TrainArgs& train;
    ScenarioRunner& runner;
};

}

#endif
