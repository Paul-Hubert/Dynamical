#ifndef SYSTEM_SET_H
#define SYSTEM_SET_H

#include <vector>
#include <memory>
#include <string>

#include "system.h"
#include "entt/entt.hpp"

namespace dy {

// World context flag: a killed agent waits for the end of its decision window (simulation.md, parking)
struct Parked {
    bool value = false;
};

class SystemSet {
public:
    // Pre and post tick once per frame; the fixed lane ticks a world at its physics rate
    enum class List : uint8_t { Pre, Post, Fixed };

    SystemSet(entt::registry& reg) : reg(reg) {}

    template<typename T, typename std::enable_if<std::is_base_of<System, T>::value>::type* = nullptr>
    void pre_add() {
        add(List::Pre, std::make_unique<T>(reg));
    }

    template<typename T, typename std::enable_if<std::is_base_of<System, T>::value>::type* = nullptr>
    void post_add() {
        add(List::Post, std::make_unique<T>(reg));
    }

    // name: the registry name (scenes), System::name() when empty. Phases only matter in the fixed lane
    void add(List list, std::unique_ptr<System> system, Phase phase = Phase::Every, std::string name = {});

    void preinit();

    void init();

    void pre_tick(double dt);

    void post_tick(double dt);

    // One fixed tick: Every systems, plus Act systems on the first tick of a decision window and Observe systems
    // on its last. While the world is Parked only the Observe systems run
    void fixed_tick(double h, bool act_tick, bool observe_tick);

    // Observe systems only (RESET: fresh observations without stepping)
    void fixed_observe_only();

    void finish();

    std::vector<std::string> names(List list) const;

private:
    struct Entry {
        std::unique_ptr<System> system;
        Phase phase;
        std::string name;
    };

    std::vector<Entry>& entries(List list);

    entt::registry& reg;
    std::vector<Entry> pre_systems;
    std::vector<Entry> post_systems;
    std::vector<Entry> fixed_systems;

};

}

#endif
