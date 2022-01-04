#include "wander_action.h"

#include <extra/optick/optick.h>

#include "logic/map/map_manager.h"

#include "logic/components/path.h"

using namespace dy;

std::unique_ptr<Action> WanderAction::deploy(std::unique_ptr<Action> self) {
    
    return self;
}

std::unique_ptr<Action> WanderAction::act(std::unique_ptr<Action> self) {
    
    OPTICK_EVENT();
    
    auto position = reg.get<Position>(entity);
    
    auto& map = reg.ctx<MapManager>();
    
    if(!reg.all_of<Path>(entity)) {
        
        const int area = 20;
        glm::vec2 target;
        Tile* tile;
        do {
            target = position + glm::vec2(rand()%area - area/2, rand()%area - area/2);
            tile = map.getTile(target);
        } while(tile == nullptr || Tile::terrain_speed.at(tile->terrain) == 0);
        
        double angle = (double) rand() / (RAND_MAX) * 2 * M_PI;
        
        glm::vec2 direction = glm::vec2(sin(angle), cos(angle));
        
        reg.emplace<Path>(entity, map.pathfind(position, [&](glm::vec2 pos) {
            return glm::dot(direction, pos - position) > 5;
        }, 1000));
        
    }
    
    return self;
    
}
