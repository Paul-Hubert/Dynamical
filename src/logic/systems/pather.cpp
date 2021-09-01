#include "system_list.h"

#include "logic/components/path.h"
#include "logic/components/position.h"
#include "logic/components/time.h"

#include "logic/map/map_manager.h"

using namespace dy;

constexpr float base_speed = 1.0 / 60.; // tiles per second

void PatherSys::preinit() {
    
}

void PatherSys::init() {
    
}

void PatherSys::tick(float dt) {
    
    auto& map = reg.ctx<MapManager>();
    auto& time = reg.ctx<Time>();
    
    auto view = reg.view<Path, Position>();
    
    for(auto entity : view) {
        auto& path = view.get<Path>(entity);
        glm::vec2 pos = view.get<Position>(entity);
        
        float base_distance = base_speed * time.dt;
        while(base_distance > 0) {
            if(path.tiles.empty()) {
                reg.remove<Path>(entity);
                break;
            }
            glm::vec2 next_pos = path.tiles.back() + 0.5f;
            glm::vec2 diff = next_pos - pos;
            float dist = glm::length(diff);
            float speed = Tile::terrain_speed.at(map.getTile(next_pos)->terrain);
            float weighted_dist = dist / speed;
            if(base_distance > weighted_dist) {
                base_distance -= weighted_dist;
                pos = next_pos;
                path.tiles.pop_back();
            } else {
                pos += diff / dist * base_distance * speed;
                break;
            }
        }
        
        map.move(entity, pos);
        
    }
    
}

void PatherSys::finish() {
    
}
