#include "selection.h"

#include "logic/components/inputc.h"
#include "logic/components/positionc.h"
#include "logic/components/selectionc.h"
#include "logic/components/renderablec.h"

#include "util/log.h"

#include "logic/map/map_manager.h"

#include <imgui.h>

void SelectionSys::preinit() {
    reg.set<SelectionC>();
}

void SelectionSys::tick(float dt) {
    
    auto& selection = reg.ctx<SelectionC>();
    
    auto& input = reg.ctx<InputC>();
    
    if(input.leftClick) {
        auto& map = reg.ctx<MapManager>();
        
        glm::vec2 pos = map.getMousePosition();
        
        
        Chunk* chunk = map.getChunk(map.getChunkPos(pos));
        if(chunk == nullptr) return;
        
        bool selected = false;
        for(auto entity : chunk->getObjects()) {
            if(reg.all_of<RenderableC>(entity)) {
                
                auto& position = reg.get<PositionC>(entity);
                auto& renderable = reg.get<RenderableC>(entity);
                
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
    auto& selection = reg.ctx<SelectionC>();
    selection.entity = entity;
    
    auto& renderable = reg.get<RenderableC>(selection.entity);
    selection.color = renderable.color;
    renderable.color = glm::vec4(0.2,0.2,0.2,1.0);
    
}

void SelectionSys::unselect() {
    auto& selection = reg.ctx<SelectionC>();
    if(selection.entity != entt::null) {
        reg.get<RenderableC>(selection.entity).color = selection.color;
    }
}

