#include "system_set.h"

#include "util/util.h"

using namespace dy;

std::vector<SystemSet::Entry>& SystemSet::entries(List list) {
    switch(list) {
    case List::Pre: return pre_systems;
    case List::Post: return post_systems;
    default: return fixed_systems;
    }
}

void SystemSet::add(List list, std::unique_ptr<System> system, Phase phase, std::string name) {
    if(name.empty()) name = system->name();
    entries(list).push_back({ std::move(system), phase, std::move(name) });
}

void SystemSet::preinit() {

    for(auto* list : { &pre_systems, &post_systems, &fixed_systems }) {
        for(Entry& e : *list) {
            e.system->preinit();
        }
    }

}

void SystemSet::init() {

    for(auto* list : { &pre_systems, &post_systems, &fixed_systems }) {
        for(Entry& e : *list) {
            e.system->init();
        }
    }

}

void SystemSet::pre_tick(double dt) {

    OPTICK_EVENT();

    for(Entry& e : pre_systems) {
        e.system->tick(dt);
    }

}

void SystemSet::post_tick(double dt) {

    OPTICK_EVENT();

    for(Entry& e : post_systems) {
        e.system->tick(dt);
    }

}

void SystemSet::fixed_tick(double h, bool act_tick, bool observe_tick) {

    OPTICK_EVENT();

    auto* parked = reg.ctx().find<Parked>();
    bool skip = parked && parked->value;

    for(Entry& e : fixed_systems) {
        bool run = false;
        switch(e.phase) {
        case Phase::Every: run = !skip; break;
        case Phase::Act: run = !skip && act_tick; break;
        case Phase::Observe: run = observe_tick; break;
        }
        if(run) e.system->tick(h);
    }

}

void SystemSet::fixed_observe_only() {

    for(Entry& e : fixed_systems) {
        if(e.phase == Phase::Observe) e.system->tick(0.);
    }

}

void SystemSet::finish() {

    for(auto* list : { &pre_systems, &post_systems, &fixed_systems }) {
        for(Entry& e : *list) {
            e.system->finish();
        }
    }

}

std::vector<std::string> SystemSet::names(List list) const {
    const auto& l = list == List::Pre ? pre_systems : list == List::Post ? post_systems : fixed_systems;
    std::vector<std::string> out;
    for(auto& e : l) out.push_back(e.name);
    return out;
}
