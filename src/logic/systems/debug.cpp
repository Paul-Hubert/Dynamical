#include "system_list.h"

#include "logic/components/inputc.h"
#include "logic/components/show_debug.h"

void DebugSys::preinit() {

}

void DebugSys::init() {
    
}

void DebugSys::tick() {
    
    InputC& input = reg.ctx<InputC>();
    if(input.on[Action::DEBUG]) {
        if(reg.try_ctx<ShowDebug>() == nullptr) {
            reg.set<ShowDebug>();
            input.mouseFree = true;
        } else {
            reg.unset<ShowDebug>();
            input.mouseFree = false;
        }
        input.on.set(Action::DEBUG, false);
    }
    
}

void DebugSys::finish() {

}
