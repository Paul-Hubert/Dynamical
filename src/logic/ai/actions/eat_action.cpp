#include "eat_action.h"

#include "logic/map/map_manager.h"

#include "util/log.h"

#include "logic/components/pathc.h"
#include "logic/components/basic_needs.h"
#include <logic/components/plant.h>

float EatAction::getScore(entt::registry& reg, entt::entity entity) {
    float hunger = reg.get<BasicNeeds>(entity).hunger;
    return hunger > 5 ? hunger : 0;
}

void EatAction::act(const PositionC position) {
    
    interruptible = false;
    
    if(phase == 0) {
    
        if(reg.all_of<PathC>(entity)) {
            reg.remove<PathC>(entity);
        }
        
        findFood(position);
        
        phase = 1;
        
    } else if(phase == 1) {
        
        if(!reg.all_of<PathC>(entity)) {
            
            reg.get<BasicNeeds>(entity).hunger = 0;
            interruptible = true;
            phase = 0;
            
        }
        
    }
    
}

void EatAction::findFood(const PositionC position) {
    
    const auto& map = reg.ctx<const MapManager>();
    
    reg.emplace<PathC>(entity, map.pathfind(position, [&](glm::vec2 pos) {
        auto object = map.getTile(pos)->object;
        return object != entt::null && reg.all_of<Plant>(object) && reg.get<Plant>(object).type == Plant::berry_bush;
    }));
    
}

