#include "ui.h"

#include <imgui/imgui.h>

#include "util/log.h"

#include "renderer/context/context.h"

#include "logic/components/input.h"

using namespace dy;

UISys::UISys(entt::registry& reg) : System(reg) {
    
    g_MouseCursors[ImGuiMouseCursor_Arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    g_MouseCursors[ImGuiMouseCursor_TextInput] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
    g_MouseCursors[ImGuiMouseCursor_ResizeAll] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    g_MouseCursors[ImGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    g_MouseCursors[ImGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    g_MouseCursors[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    g_MouseCursors[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    g_MouseCursors[ImGuiMouseCursor_Hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    
}

void UISys::tick(float dt) {
    
    OPTICK_EVENT();
    
    auto& input = reg.ctx<Input>();
    
    ImGuiIO& io = ImGui::GetIO();

    io.MouseDown[0] = input.mouseLeft;
    io.MouseDown[1] = input.mouseRight;
    io.MouseDown[2] = input.mouseMiddle;

    if(auto text = input.text) {
        io.AddInputCharactersUTF8((*text).data());
        input.text = std::nullopt;
    }

    io.AddKeyEvent(ImGuiKey_Backspace, input.on[Input::BACKSPACE]);
    io.AddKeyEvent(ImGuiKey_Tab, input.on[Input::TAB]);

    if(input.focused)
        io.MousePos = ImVec2((float) input.mousePos.x, (float) input.mousePos.y);

    io.DisplaySize = ImVec2((float)input.screenSize.x, (float)input.screenSize.y);
    
    static Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 current_time = SDL_GetPerformanceCounter();
    io.DeltaTime = g_Time > 0 ? (float)((double)(current_time - g_Time) / frequency) : (float)(1.0f / 60.0f);
    g_Time = current_time;

    ImGui::NewFrame();
    
    if(io.WantCaptureMouse) {
        input.leftClick = false;
        input.rightClick = false;
        input.middleClick = false;
        input.leftDown = false;
        input.rightDown = false;
        input.middleDown = false;
    } else {
        input.leftDown = input.mouseLeft;
        input.rightDown = input.mouseRight;
        input.middleDown = input.mouseMiddle;
    }


    
}

UISys::~UISys() {

}
