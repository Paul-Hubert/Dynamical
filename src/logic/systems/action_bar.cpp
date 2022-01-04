#include "system_list.h"

#include <imgui.h>

#include <logic/components/action_bar.h>
#include <logic/components/position.h>
#include <logic/components/camera.h>
#include <logic/components/input.h>

using namespace dy;

void ActionBarSys::preinit() {
    
}

void ActionBarSys::init() {
    
}

void ActionBarSys::tick(float dt) {
    
    OPTICK_EVENT();
    
    auto& cam = reg.ctx<Camera>();
    auto& input = reg.ctx<Input>();
    auto& time = reg.ctx<Time>();
    
    int id = 0;
    
    char buffer[50];
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    
    auto view = reg.view<ActionBar, Position>();
    view.each([&](auto entity, auto& action_bar, auto position) {
        
        constexpr float margin = 100;
        glm::vec2 pos = cam.fromWorldSpace(position, input.screenSize);
        if(pos.x < -margin || pos.y < -margin || pos.x > input.screenSize.x + margin || pos.y > input.screenSize.y + margin) {
            return;
        }
        
        sprintf(buffer, "NULL###ActionBar%i", id);
        if(ImGui::Begin(buffer, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground)) {
            glm::vec2 size = cam.fromWorldSize(glm::vec2(1.0, 0.2), input.screenSize);
            ImVec2 real_size = ImGui::GetWindowSize();
            ImGui::SetWindowPos(ImVec2(pos.x - size.x/2, pos.y - size.y/2 + size.x/2));
            ImGui::ProgressBar((float) (time.current - action_bar.start) / (action_bar.end - action_bar.start), ImVec2(size.x, size.y), "");
        }
        ImGui::End();
        
        id++;
        
    });
    
    ImGui::PopStyleVar();
    
}

void ActionBarSys::finish() {
    
}
