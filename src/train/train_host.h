#ifndef DY_TRAIN_HOST_H
#define DY_TRAIN_HOST_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include "env_params.h"
#include "environment.h"
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

    // mode=train: the scene's agent worlds (WorldsEnvironment), recreated with the trainer's seed
    int agents();

    // mode=record-demo: plays the scene's track kinematically and writes a .demo of its observations (zero actions).
    // Scene variables: decisions (a minimum: recording ends with the episode in progress), out
    int recordDemo();

    // mode=eval: the scene's Policy worlds for var.episodes episodes, deterministic, a summary JSON at var.out
    int eval();

    // The animals (animal_host.cpp): is this a scene of AnimalBot worlds, the checks of a recording as JSON
    // (var.checks), the eval of a policy on them
    bool animalWorlds();
    std::string animalDemoProblem();
    void recordAnimalReplay();
    std::vector<float> animal_replay;
    void writeAnimalChecks();
    int evalAnimals();

    // The sandbox (sandbox_host.cpp): is this a scene of sandbox worlds; mode=eval on one: var.seconds of what its
    // policies and scripted minds do, a summary JSON at var.out, the animals from above at var.replay; mode=signals:
    // what a reward file can read
    bool sandboxWorlds();
    int evalSandbox();
    int signals();

    // The handshake, then RESET / STEP / QUIT against the environment `connected` makes from the trainer's seed
    int serve(const std::function<std::unique_ptr<TrainerEnvironment>(int64_t seed)>& connected);

    // -envParams= and -set:, before anything else (a missing file is fatal, as in Unity)
    bool loadEnvParams();
    void writeEnvParams(int64_t steps);
    // The resolved environment parameters, as the worlds read them at each episode start
    void pushEnvParams();

    const RunArgs& args;
    const TrainArgs& train;
    ScenarioRunner& runner;
    EnvParamStore env_params;
    std::atomic<int64_t> base_seed { -1 };
    std::string behavior = "HeidelBotBehavior";     // envparams.txt header
    std::map<std::string, float> written;
    bool envparams_written = false;
};

}

#endif
