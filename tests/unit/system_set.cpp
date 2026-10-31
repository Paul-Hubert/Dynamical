#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "logic/systems/system_set.h"

using namespace dy;

namespace {

struct Tick {
    int value = 0;
};

// Logs "<label>@<tick>" with the world tick from the context
class Recorder : public System {
public:
    Recorder(entt::registry& reg, std::string label, std::vector<std::string>& log) : System(reg), label(std::move(label)), log(log) {}
    void tick(double) override { log.push_back(label + "@" + std::to_string(reg.ctx().get<Tick>().value)); }
    const char* name() override { return "Recorder"; }
    std::string label;
    std::vector<std::string>& log;
};

}

TEST_SUITE("unit") {

TEST_CASE("T1.6 SystemSet fixed lane: Act only at k = 0 and Observe only at k = period - 1 over 10 windows") {

    entt::registry reg;
    reg.ctx().emplace<Tick>();
    std::vector<std::string> log;

    SystemSet set(reg);
    set.add(SystemSet::List::Fixed, std::make_unique<Recorder>(reg, "act", log), Phase::Act, "Act");
    set.add(SystemSet::List::Fixed, std::make_unique<Recorder>(reg, "every", log), Phase::Every, "Every");
    set.add(SystemSet::List::Fixed, std::make_unique<Recorder>(reg, "observe", log), Phase::Observe, "Observe");
    CHECK(set.names(SystemSet::List::Fixed) == std::vector<std::string> { "Act", "Every", "Observe" });

    const int period = 4, windows = 10;
    std::vector<std::string> expected;
    for(int w = 0; w < windows; w++) {
        for(int k = 0; k < period; k++) {
            int t = w * period + k;
            reg.ctx().get<Tick>().value = t;
            set.fixed_tick(1. / 60., k == 0, k == period - 1);
            if(k == 0) expected.push_back("act@" + std::to_string(t));
            expected.push_back("every@" + std::to_string(t));
            if(k == period - 1) expected.push_back("observe@" + std::to_string(t));
        }
    }
    CHECK(log == expected);

}

TEST_CASE("T1.6 SystemSet fixed lane: a parked world runs only Observe and fixed_observe_only runs only Observe") {

    entt::registry reg;
    reg.ctx().emplace<Tick>();
    std::vector<std::string> log;

    SystemSet set(reg);
    set.add(SystemSet::List::Fixed, std::make_unique<Recorder>(reg, "act", log), Phase::Act);
    set.add(SystemSet::List::Fixed, std::make_unique<Recorder>(reg, "every", log), Phase::Every);
    set.add(SystemSet::List::Fixed, std::make_unique<Recorder>(reg, "observe", log), Phase::Observe);

    // Killed at tick 0 of a window of 2: the rest of the window skips the simulation, the observation still leaves
    set.fixed_tick(1., true, false);
    reg.ctx().emplace<Parked>().value = true;
    reg.ctx().get<Tick>().value = 1;
    set.fixed_tick(1., false, true);
    CHECK(log == std::vector<std::string> { "act@0", "every@0", "observe@1" });

    log.clear();
    reg.ctx().get<Parked>().value = false;
    set.fixed_observe_only();
    CHECK(log == std::vector<std::string> { "observe@1" });

}

TEST_CASE("SystemSet keeps registry names and master's typed adds") {

    entt::registry reg;
    std::vector<std::string> log;
    SystemSet set(reg);
    set.add(SystemSet::List::Pre, std::make_unique<Recorder>(reg, "a", log), Phase::Every, "Time");
    set.add(SystemSet::List::Pre, std::make_unique<Recorder>(reg, "b", log));
    CHECK(set.names(SystemSet::List::Pre) == std::vector<std::string> { "Time", "Recorder" });
    CHECK(set.names(SystemSet::List::Post).empty());

}

}
