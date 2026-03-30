#include "speech_bubble_render_sys.h"

#include <imgui/imgui.h>
#include <ai/speech/speech_bubble.h>
#include <logic/components/position.h>
#include <logic/components/camera.h>
#include <logic/components/input.h>

using namespace dy;

void SpeechBubbleRenderSys::tick(double dt) {
    OPTICK_EVENT();

    auto& cam = reg.ctx<Camera>();
    auto& input = reg.ctx<Input>();

    int id = 0;
    char buffer[50];

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

    auto view = reg.view<SpeechBubble, Position>();
    view.each([&](auto entity, auto& bubble, auto position) {
        // Project world position to screen
        constexpr float margin = 150;
        glm::vec2 pos = cam.fromWorldSpace(position.getVec3());

        // Skip if off-screen
        if (pos.x < -margin || pos.y < -margin ||
            pos.x > input.screenSize.x + margin ||
            pos.y > input.screenSize.y + margin) {
            return;
        }

        // Create unique window ID
        sprintf(buffer, "SpeechBubble###%i", id);

        // Render ImGui window with auto-sizing and no decoration
        if (ImGui::Begin(buffer, nullptr,
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoBackground)) {

            // Render thought (if present, dimmer color)
            if (!bubble.thought.empty()) {
                ImGui::TextWrapped("%s", bubble.thought.c_str());
            }

            // Render dialogue (if present, bright/yellow)
            if (!bubble.dialogue.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f),
                                   "%s", bubble.dialogue.c_str());
            }

            // Position window above entity
            ImVec2 size = ImGui::GetWindowSize();
            ImGui::SetWindowPos(ImVec2(pos.x - size.x / 2, pos.y - size.y));
        }
        ImGui::End();

        id++;
    });

    ImGui::PopStyleVar();
}
