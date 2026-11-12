#ifndef DY_PROFILE_WORKERS_H
#define DY_PROFILE_WORKERS_H

#include "profile.h"

#include <taskflow/taskflow.hpp>

#include <cstdio>
#include <memory>

namespace dy::profile {

// Gives the workers of an executor their names in the profiler ("Worker 3") :
// tf::Executor executor(threads, profile::namedWorkers());
class NamedWorkers : public tf::WorkerInterface {
public:
    explicit NamedWorkers(const char* prefix) : prefix(prefix) {}

    void scheduler_prologue(tf::Worker& worker) override {
        char name[48];
        std::snprintf(name, sizeof(name), "%s %zu", prefix, worker.id());
        threadName(name);
    }

    void scheduler_epilogue(tf::Worker&, std::exception_ptr) override {}

private:
    const char* prefix;
};

inline std::unique_ptr<tf::WorkerInterface> namedWorkers(const char* prefix = "Worker") {
    return std::make_unique<NamedWorkers>(prefix);
}

}

#endif
