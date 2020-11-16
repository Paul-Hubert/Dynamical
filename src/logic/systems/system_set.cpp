#include "system_set.h"

#include "util/util.h"

void SystemSet::add(System* system) {
    systems.push_back(system);
}

void SystemSet::preinit() {

    Util::log(Util::trace) << "Preinitializing system set\n";
    
    for(System* sys : systems) {
        sys->preinit();
    }
    
}

void SystemSet::init() {
    
    Util::log(Util::trace) << "Initializing system set\n";

    for(System* sys : systems) {
        sys->init();
    }
    
}

void SystemSet::tick() {
    
    for(System* sys : systems) {
        sys->tick();
    }
    
}

void SystemSet::finish() {

    Util::log(Util::trace) << "Finishing system set\n";

    for(System* sys : systems) {
        sys->finish();
    }
    
}

SystemSet::~SystemSet() {

    Util::log(Util::trace) << "Destroying system set\n";

}
