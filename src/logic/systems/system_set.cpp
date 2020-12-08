#include "system_set.h"

#include "util/util.h"

void SystemSet::preinit() {

    for(std::unique_ptr<System>& sys : pre_systems) {
        sys->preinit();
    }

    for (std::unique_ptr<System>& sys : post_systems) {
        sys->preinit();
    }
    
}

void SystemSet::init() {

    for (std::unique_ptr<System>& sys : pre_systems) {
        sys->init();
    }

    for (std::unique_ptr<System>& sys : post_systems) {
        sys->init();
    }
    
}

void SystemSet::pre_tick(float dt) {

    OPTICK_EVENT();
    
    for(std::unique_ptr<System>& sys : pre_systems) {
        sys->tick(dt);
    }
    
}

void SystemSet::post_tick(float dt) {

    OPTICK_EVENT();

    for (std::unique_ptr<System>& sys : post_systems) {
        sys->tick(dt);
    }

}

void SystemSet::finish() {

    for (std::unique_ptr<System>& sys : pre_systems) {
        sys->finish();
    }

    for (std::unique_ptr<System>& sys : post_systems) {
        sys->finish();
    }
    
}
