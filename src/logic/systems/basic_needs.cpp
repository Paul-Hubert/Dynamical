#include "system_list.h"

#include "logic/components/timec.h"
#include "logic/components/basic_needs.h"

void BasicNeedsSys::preinit() {
    
}

void BasicNeedsSys::init() {
    
}

void BasicNeedsSys::tick(float dt) {
    
    auto& time = reg.ctx<TimeC>();
    
    auto view = reg.view<BasicNeeds>();
    
    view.each([&](BasicNeeds& needs) {
        needs.hunger += 10.0 / (Date::seconds_in_a_minute * Date::minutes_in_an_hour * Date::hours_in_a_day) * time.dt;
    });
    
}

void BasicNeedsSys::finish() {
    
}
