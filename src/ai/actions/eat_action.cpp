#include "eat_action.h"

#include <extra/optick/optick.h>

#include "logic/map/map_manager.h"

#include "util/log.h"

#include "harvest_action.h"

#include "logic/components/path.h"
#include <logic/components/storage.h>
#include <logic/components/eat.h>
#include <logic/components/item.h>

using namespace entt::literals;

using namespace dy;

std::unique_ptr<Action> EatAction::act_impl(std::unique_ptr<Action> self) {
    
    OPTICK_EVENT();
    
    if(phase == 0) {
        
        find();
        if(food == Item::null) {
            return nullptr;
        }
        reg.emplace<Eat>(entity, reg, food);
        phase = 1;
        
    } else if(phase == 1) {
        
        if(!reg.all_of<Eat>(entity)) {
            
            return nullptr;
            
        }
        
    }
    
    return self;
    
}

void EatAction::find() {
    
    const Storage& storage = reg.get<Storage>(entity);

    food = Item::null;

    if(storage.amount(Item::berry) > 0) {
        food = Item::berry;
    }
    
}

