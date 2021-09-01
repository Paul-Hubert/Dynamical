#include "wander_action.h"

#include "logic/map/map_manager.h"

#include "logic/components/path.h"

using namespace dy;

float WanderAction::getScore(entt::registry& reg, entt::entity entity) {
    return 1;
}

void WanderAction::act(const Position position) {
    
    const auto& map = reg.ctx<const MapManager>();
    
    if(!reg.all_of<Path>(entity)) {
        
        const int area = 20;
        glm::vec2 target;
        Tile* tile;
        do {
            target = position + glm::vec2(rand()%area - area/2, rand()%area - area/2);
            tile = map.getTile(target);
        } while(tile == nullptr || Tile::terrain_speed.at(tile->terrain) == 0);
        
        reg.emplace<Path>(entity, map.pathfind(position, [&](glm::vec2 pos) {
            return map.floor(target) == map.floor(pos);
        }));
        
    }
}
