#include "system_list.h"

#include "logic/components/time.h"
#include "logic/components/input.h"

#include <imgui/imgui.h>

using namespace dy;

void TimeSys::preinit() {
    reg.set<Time>();
}

void TimeSys::init() {
    
}

void TimeSys::tick(float dt) {
    
    OPTICK_EVENT();
    
    auto& time = reg.ctx<Time>();
    
    auto& input = reg.ctx<Input>();
    if(input.on[Input::PAUSE]) {
        if(time.speed_modifier == 0) time.speed_modifier = time.last_speed_modifier;
        else time.speed_modifier = 0;
        input.on[Input::PAUSE] = false;
    }
    
    static bool open = true;
    if(ImGui::Begin("Time", &open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {// ImGuiWindowFlags_NoTitleBar
        ImVec2 size = ImGui::GetWindowSize();
        ImGui::SetWindowPos(ImVec2(input.screenSize.x - size.x, 0));
        
        Date date(time.current);
        ImGui::Text("Time : %lu of %s, %lu %lu:%lu:%lu", date.days+1, Date::names_of_months[date.months], date.years, date.hours, date.minutes, date.seconds);
        
        int mode = time.speed_modifier;
        ImGui::Text("Speed : "); ImGui::SameLine();
        ImGui::RadioButton("0", &mode, 0); ImGui::SameLine();
        ImGui::RadioButton("1", &mode, 1); ImGui::SameLine();
        ImGui::RadioButton("2", &mode, 3); ImGui::SameLine();
        ImGui::RadioButton("3", &mode, 10); ImGui::SameLine();
        ImGui::RadioButton("4", &mode, 50);
        time.speed_modifier = mode;
        
        if(time.speed_modifier > 0) time.last_speed_modifier = time.speed_modifier;
        
    }
    ImGui::End();
    
    time.dt = time.speed * time.speed_modifier * (double) dt;
    time.current += time.dt;
    
}

void TimeSys::finish() {
    
}
