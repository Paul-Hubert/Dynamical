#include "ai.h"

#include "aic.h"
#include "logic/components/positionc.h"
#include <logic/map/map_manager.h>
#include <logic/components/pathc.h>

AISys::AISys(entt::registry& reg) : System(reg) {
    
}

void AISys::tick(float dt) {
    
    const auto& map = reg.ctx<const MapManager>();
    
    auto view = reg.view<AIC, const PositionC>();
    
    view.each([&](const auto entity, const auto& ai, const auto position) {
        
        if(!reg.all_of<PathC>(entity)) {
            
            const int area = 20;
            glm::vec2 target;
            Tile* tile;
            do {
                target = position + glm::vec2(rand()%area - area/2, rand()%area - area/2);
                tile = map.getTile(target);
            } while(tile == nullptr || Tile::terrain_speed.at(tile->terrain) == 0);
            
            reg.emplace<PathC>(entity, map.pathfind(position, [&](glm::vec2 pos) {
                return map.floor(target) == map.floor(pos);
            }));
            
        }
        
        
    });
    
}

AISys::~AISys() {
    
}

