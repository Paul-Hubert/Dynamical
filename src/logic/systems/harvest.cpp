#include "system_list.h"

#include <logic/components/harvest.h>
#include <logic/components/storage.h>
#include <logic/components/time.h>
#include <logic/components/object.h>
#include <logic/components/item.h>
#include <logic/components/harvested.h>
#include <logic/components/action_bar.h>

using namespace entt::literals;

using namespace dy;

void HarvestSys::preinit() {
    
}

void HarvestSys::init() {
    
}

void HarvestSys::tick(double dt) {
    
    OPTICK_EVENT();
    
    constexpr time_t duration = 10 * Time::minute;
    
    auto& time = reg.ctx<Time>();
    
    auto view = reg.view<Harvest>();
    view.each([&](const auto entity, auto& harvest) {
        if(!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, harvest.start, harvest.start + duration);
        }
        
        if(time.current > harvest.start + duration) {
            auto& storage = reg.get<Storage>(entity);
            auto& object = reg.get<Object>(harvest.plant);
            
            storage.add(Item(Item::berry, 20));
            
            reg.emplace<Harvested>(harvest.plant, time.current + Time::day);
            reg.remove<Harvest>(entity);
            reg.remove<ActionBar>(entity);
        }
    });
    
    auto view2 = reg.view<Harvested>();
    view2.each([&](const auto entity, auto& harvested) {
        if(time.current > harvested.end) {
            reg.remove<Harvested>(entity);
        }
    });
    
    
}

void HarvestSys::finish() {
    
}
