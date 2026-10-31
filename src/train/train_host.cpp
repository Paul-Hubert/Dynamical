#include "train_host.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

#include "util/log.h"

using namespace dy;

int TrainHost::run() {

    std::string mode = args.mode.empty() ? "train" : args.mode;
    if(mode == "bench") return bench();

    dy::log(dy::error) << "mode=" << mode << " is not available yet\n";
    return 1;

}

int TrainHost::bench() {

    const int64_t ticks = args.ticks > 0 ? args.ticks : 600;
    const size_t worlds = runner.worldCount();
    const unsigned threads = args.threads > 0 ? (unsigned) args.threads
        : (unsigned) std::max<size_t>(1, std::min<size_t>(worlds, std::thread::hardware_concurrency()));

    tf::Executor executor(threads);
    auto start = std::chrono::steady_clock::now();
    runner.step(ticks, &executor);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    double rate = seconds > 0. ? ticks / seconds : 0.;
    dy::log(dy::info) << "bench " << runner.scene().name << ": " << worlds << " worlds, " << threads << " threads, " << ticks
        << " ticks in " << seconds << " s: " << rate << " ticks/s, " << rate * worlds << " world-ticks/s\n";
    return 0;

}
