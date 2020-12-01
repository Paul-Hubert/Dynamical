#include "system_set.h"

#include "util/util.h"

void SystemSet::preinit() {

    for(std::unique_ptr<System>& sys : systems) {
        sys->preinit();
    }
    
}

void SystemSet::init() {

    for(std::unique_ptr<System>& sys : systems) {
        sys->init();
    }
    
}

void SystemSet::tick() {

    OPTICK_EVENT();
    
    for(std::unique_ptr<System>& sys : systems) {
        sys->tick();
    }
    
}

void SystemSet::finish() {

    for(std::unique_ptr<System>& sys : systems) {
        sys->finish();
    }
    
}
