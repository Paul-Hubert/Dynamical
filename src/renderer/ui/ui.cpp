#include "ui.h"

#include "renderer/context/context.h"

#include "logic/components/inputc.h"

UI::UI(entt::registry& reg) : reg(reg) {

    g_MouseCursors[ImGuiMouseCursor_Arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    g_MouseCursors[ImGuiMouseCursor_TextInput] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
    g_MouseCursors[ImGuiMouseCursor_ResizeAll] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    g_MouseCursors[ImGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    g_MouseCursors[ImGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    g_MouseCursors[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    g_MouseCursors[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    g_MouseCursors[ImGuiMouseCursor_Hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

    entt::monostate<"imgui_frame"_hs>{} = false;

}

void UI::prepare() {

    OPTICK_EVENT();
    
    auto& input = reg.ctx<InputC>();
    
    Context& ctx = *reg.ctx<Context*>();
    
    ImGuiIO& io = ImGui::GetIO();

    io.MouseDown[0] = input.mouseLeft || input.on[Action::PRIMARY];
    io.MouseDown[1] = input.mouseRight || input.on[Action::SECONDARY];
    io.MouseDown[2] = input.mouseMiddle || input.on[Action::TERTIARY];

    if(input.focused)
        io.MousePos = ImVec2((float) input.mousePos.x, (float) input.mousePos.y);

    int w, h;
    SDL_GetWindowSize(ctx.win, &w, &h);
    io.DisplaySize = ImVec2((float)w, (float)h);
    
    static Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 current_time = SDL_GetPerformanceCounter();
    io.DeltaTime = g_Time > 0 ? (float)((double)(current_time - g_Time) / frequency) : (float)(1.0f / 60.0f);
    g_Time = current_time;

    if(entt::monostate<"imgui_frame"_hs>{}) ImGui::Render();

    ImGui::NewFrame();
    entt::monostate<"imgui_frame"_hs>{} = true;
    
}

void UI::render() {



}

UI::~UI() {
    
    for (ImGuiMouseCursor cursor_n = 0; cursor_n < ImGuiMouseCursor_COUNT; cursor_n++)
        SDL_FreeCursor(g_MouseCursors[cursor_n]);
    memset(g_MouseCursors, 0, sizeof(g_MouseCursors));
    
}
