#include "harvest_action.h"

#include <extra/optick/optick.h>

#include "logic/map/map_manager.h"

#include "util/log.h"

#include "logic/components/path.h"
#include <logic/components/object.h>
#include <logic/components/storage.h>
#include <logic/components/harvest.h>
#include <logic/components/harvested.h>

using namespace entt::literals;

using namespace dy;

std::unique_ptr<Action> HarvestAction::deploy(std::unique_ptr<Action> self, Object::Identifier plant) {
    
    this->plant = plant;
    
    return self;
    
}

std::unique_ptr<Action> HarvestAction::act(std::unique_ptr<Action> self) {
    
    OPTICK_EVENT();
    
    if(phase == 0) {
        
        if(reg.all_of<Path>(entity)) {
            reg.remove<Path>(entity);
        }
        
        find();
        
        if(target == entt::null) {
            return nextAction();
        }
        
        reg.emplace<entt::tag<"reserved"_hs>>(target);
        
        phase = 1;
        
    }  else if(phase == 1) {
        
        if(!reg.all_of<Path>(entity)) {
            
            reg.emplace<Harvest>(entity, reg, target);
            phase = 2;
            
        }
        
    } else if (phase == 2) {
        
        if(!reg.all_of<Harvest>(entity)) {
            
            reg.remove<entt::tag<"reserved"_hs>>(target);
            
            return nextAction();
            
        }
        
    }
    
    return self;
    
}

void HarvestAction::find() {
    
    auto& map = reg.ctx<MapManager>();
    
    auto position = reg.get<Position>(entity);
    
    reg.emplace<Path>(entity, map.pathfind(position, [&](glm::vec2 pos) {
        auto object = map.getTile(pos)->object;
        if(object != entt::null && reg.all_of<Object>(object) && reg.get<Object>(object).id == plant && !reg.all_of<entt::tag<"reserved"_hs>>(object) && !reg.all_of<Harvested>(object)) {
            target = object;
            return true;
        }
        return false;
    }));
    
}


