#include "system_list.h"

#include <logic/components/harvest.h>
#include <logic/components/storage.h>
#include <logic/components/time.h>
#include <logic/components/plant.h>
#include <logic/components/item.h>
#include <logic/components/harvested.h>

using namespace entt::literals;

using namespace dy;

void HarvestSys::preinit() {
    
}

void HarvestSys::init() {
    
}

void HarvestSys::tick(float dt) {
    
    OPTICK_EVENT();
    
    auto& time = reg.ctx<Time>();
    
    auto view = reg.view<Harvest>();
    view.each([&](const auto entity, auto& harvest) {
        if(time.current > harvest.start + 10 * Time::minute) {
            auto& storage = reg.get<Storage>(entity);
            auto& plant = reg.get<Plant>(harvest.plant);
            
            storage.add(Item(Item::berry, 20));
            
            reg.emplace<Harvested>(harvest.plant, time.current + Time::day);
            reg.remove<Harvest>(entity);
            
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
