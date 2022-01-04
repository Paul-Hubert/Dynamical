#include "selection.h"

#include "logic/components/input.h"
#include "logic/components/position.h"
#include "logic/components/selection.h"
#include "logic/components/renderable.h"

#include "util/log.h"

#include "logic/map/map_manager.h"

#include <imgui.h>

using namespace dy;

void SelectionSys::preinit() {
    reg.set<Selection>();
}

void SelectionSys::tick(float dt) {
    
    OPTICK_EVENT();
    
    auto& selection = reg.ctx<Selection>();
    
    auto& input = reg.ctx<Input>();
    
    if(input.leftClick) {
        auto& map = reg.ctx<MapManager>();
        
        glm::vec2 pos = map.getMousePosition();
        
        
        Chunk* chunk = map.getChunk(map.getChunkPos(pos));
        if(chunk == nullptr) return;
        
        bool selected = false;
        for(auto entity : chunk->getObjects()) {
            if(reg.all_of<Renderable>(entity)) {
                
                auto& position = reg.get<Position>(entity);
                auto& renderable = reg.get<Renderable>(entity);
                
                if(pos.x >= position.x - renderable.size.x/2 && position.x + renderable.size.x/2 >= pos.x && pos.y >= position.y - renderable.size.y/2 && position.y + renderable.size.y/2 >= pos.y) {
                
                    selected = true;
                    
                    unselect();
                    
                    select(entity);
                    
                    break;
                }
                
            }
        }
        
        if(!selected) unselect();
        
    }
    
    
    
}

void SelectionSys::select(entt::entity entity) {
    auto& selection = reg.ctx<Selection>();
    selection.entity = entity;
    
    auto& renderable = reg.get<Renderable>(selection.entity);
    selection.color = renderable.color;
    renderable.color = Color(0.2,0.2,0.2,1.0);
    
}

void SelectionSys::unselect() {
    auto& selection = reg.ctx<Selection>();
    if(selection.entity != entt::null) {
        reg.get<Renderable>(selection.entity).color = selection.color;
    }
}

