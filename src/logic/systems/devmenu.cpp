#include "system_list.h"

#include "logic/components/inputc.h"
#include <logic/map/map_manager.h>

#include "logic/factories/factory_list.h"

#include <imgui.h>

void DevMenuSys::preinit() {
    
}

void DevMenuSys::init() {
    
}

void DevMenuSys::tick(float dt) {
    
    auto& input = reg.ctx<InputC>();
    
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    static bool open = true;
    if(ImGui::Begin("Object Creator", &open)) {
        static int option = 0;
        ImGui::RadioButton("Nothing", &option, 0);
        ImGui::RadioButton("Tree", &option, 1);
        ImGui::RadioButton("Berry Bush", &option, 3);
        ImGui::RadioButton("Human", &option, 2);
        if(option > 0) {
            if(input.leftClick) {
                auto& map = reg.ctx<MapManager>();
                glm::vec2 pos = map.getMousePosition();
                
                if(option == 1) {
                    dy::buildTree(reg, pos);
                } else if(option == 2) {
                    dy::buildHuman(reg, pos, glm::vec4(0.7059, 0.4549, 0.2314, 1.0));
                } else if(option == 3) {
                    dy::buildBerryBush(reg, pos);
                }
                
            }
        }
    }
    ImGui::End();
    
}

void DevMenuSys::finish() {
    
}
